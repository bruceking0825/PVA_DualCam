#include "pva/opcua_worker.hpp"
#include "open62541.h"
#include <QMutexLocker>

namespace pva
{
    namespace
    {
        constexpr auto endpoint = "opc.tcp://192.168.0.1:4840";
        constexpr auto modeId = "\"Camera_Data_Global\".\"V285_Melt_Level_Mode_From_Camera\"";
        constexpr auto shoulderId = "\"Camera_Data_Global\".\"shoulderMode\"";
        constexpr auto diameterId = "\"Camera_Data_Global\".\"diameter\"";
        constexpr auto comId = "\"Camera_Data_Global\".\"com\"";

        UA_NodeId node(const char *identifier)
        {
            return UA_NODEID_STRING(3, const_cast<char *>(identifier));
        }

        std::optional<double> scalarNumber(const UA_Variant &value)
        {
            if (!UA_Variant_isScalar(&value) || !value.data)
                return {};
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN]))
                return *static_cast<UA_Boolean *>(value.data) ? 1.0 : 0.0;
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_SBYTE]))
                return *static_cast<UA_SByte *>(value.data);
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BYTE]))
                return *static_cast<UA_Byte *>(value.data);
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT16]))
                return *static_cast<UA_Int16 *>(value.data);
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT16]))
                return *static_cast<UA_UInt16 *>(value.data);
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32]))
                return *static_cast<UA_Int32 *>(value.data);
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32]))
                return *static_cast<UA_UInt32 *>(value.data);
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT64]))
                return double(*static_cast<UA_Int64 *>(value.data));
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT64]))
                return double(*static_cast<UA_UInt64 *>(value.data));
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT]))
                return *static_cast<UA_Float *>(value.data);
            if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE]))
                return *static_cast<UA_Double *>(value.data);
            return {};
        }

        bool readNumber(UA_Client *client, const char *identifier, double &output)
        {
            UA_Variant value;
            UA_Variant_init(&value);
            const auto status = UA_Client_readValueAttribute(client, node(identifier), &value);
            const auto converted = status == UA_STATUSCODE_GOOD ? scalarNumber(value) : std::optional<double>{};
            UA_Variant_clear(&value);
            if (!converted)
                return false;
            output = *converted;
            return true;
        }

        bool writeFloat(UA_Client *client, const char *identifier, float number)
        {
            UA_Variant value;
            UA_Variant_init(&value);
            UA_Variant_setScalar(&value, &number, &UA_TYPES[UA_TYPES_FLOAT]);
            return UA_Client_writeValueAttribute(client, node(identifier), &value) == UA_STATUSCODE_GOOD;
        }
    }

    OpcUaWorker::OpcUaWorker(QObject *parent) : QThread(parent) {}
    OpcUaWorker::~OpcUaWorker()
    {
        stop();
        wait();
    }

    void OpcUaWorker::stop()
    {
        stopping_ = true;
        requestInterruption();
        stopCondition_.wakeAll();
    }

    bool OpcUaWorker::waitForStop(unsigned long milliseconds)
    {
        QMutexLocker lock(&waitMutex_);
        if (stopping_ || isInterruptionRequested())
            return true;
        stopCondition_.wait(&waitMutex_, milliseconds);
        return stopping_ || isInterruptionRequested();
    }

    void OpcUaWorker::queueDiameter(double diameterMm)
    {
        QMutexLocker lock(&mutex_);
        pendingDiameter_ = diameterMm;
    }

    void OpcUaWorker::run()
    {
        stopping_ = false;
        while (!stopping_ && !isInterruptionRequested())
        {
            UA_Client *client = UA_Client_new();
            if (!client)
            {
                emit failed("Cannot create OPC UA client");
                waitForStop(2000);
                continue;
            }
            auto *clientConfig = UA_Client_getConfig(client);
            UA_ClientConfig_setDefault(clientConfig);
            // 防止模式切换刚好发生在网络读写时，UI 长时间等待 PLC 超时。
            clientConfig->timeout = 500;
            auto status = UA_Client_connect(client, endpoint);
            if (status != UA_STATUSCODE_GOOD)
            {
                emit connectionChanged(false);
                emit failed(QString("PLC connection failed: %1").arg(UA_StatusCode_name(status)));
                UA_Client_delete(client);
                waitForStop(2000);
                continue;
            }

            emit connectionChanged(true);
            while (!stopping_ && !isInterruptionRequested())
            {
                double stage = 0.0, shoulder = 0.0;
                if (!readNumber(client, modeId, stage) || !readNumber(client, shoulderId, shoulder))
                {
                    emit failed("PLC control read failed; reconnecting");
                    break;
                }
                emit controlsChanged(int(stage), shoulder != 0.0);

                std::optional<double> diameter;
                {
                    QMutexLocker lock(&mutex_);
                    diameter.swap(pendingDiameter_);
                }
                if (diameter)
                {
                    comValue_ = (comValue_ + 1) % 101;
                    if (!writeFloat(client, diameterId, float(*diameter)) || !writeFloat(client, comId, float(comValue_)))
                    {
                        emit failed("PLC value write failed; reconnecting");
                        break;
                    }
                }
                UA_Client_run_iterate(client, 0);
                if (waitForStop(100))
                    break;
            }
            UA_Client_disconnect(client);
            UA_Client_delete(client);
            emit connectionChanged(false);
            if (!stopping_)
                waitForStop(2000);
        }
    }
}
