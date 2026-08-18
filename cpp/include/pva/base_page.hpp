#pragma once

#include <QWidget>
#include <functional>

namespace pva
{
    // Mirrors modules/base_page.py. Derived pages keep the same initialization
    // phases while their Designer-generated Ui class remains strongly typed.
    class BasePage : public QWidget
    {
        Q_OBJECT
    public:
        explicit BasePage(QWidget *parent = nullptr) : QWidget(parent) {}
        ~BasePage() override = default;

    protected:
        void initializePage(const std::function<void()> &setupDesignerUi)
        {
            initializeState();
            setupDesignerUi();
            setupPageUi();
            bindEvents();
            bindSignals();
            onReady();
        }

        virtual void initializeState() = 0;
        virtual void setupPageUi() = 0;
        virtual void bindEvents() = 0;
        virtual void bindSignals() = 0;
        virtual void onReady() {}
    };
}
