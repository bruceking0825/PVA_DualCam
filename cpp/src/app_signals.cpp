#include "pva/app_signals.hpp"
namespace pva
{
    AppSignals &AppSignals::instance()
    {
        static AppSignals value;
        return value;
    }
}
