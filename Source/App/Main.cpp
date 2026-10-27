// AMP FORGE — standalone application entry point.
#include "AppController.h"
#include "../UI/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace af
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, AppController& app)
        : juce::DocumentWindow (name, ui::Colours::background, juce::DocumentWindow::allButtons), controller (app)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ui::MainComponent (app), true);
        setResizable (true, false);
        setResizeLimits (960, 640, 3000, 2000);
        const auto saved = app.getSettings().getValue ("windowState");
        if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
            centreWithSize (1280, 840);
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    ~MainWindow() override { controller.getSettings().setValue ("windowState", getWindowStateAsString()); }

private:
    AppController& controller;
};

class AmpForgeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return AMPFORGE_PRODUCT_NAME; }
    const juce::String getApplicationVersion() override { return AMPFORGE_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; } // one app owning the audio interface

    void initialise (const juce::String& commandLine) override
    {
        controller = std::make_unique<AppController>();
        controller->initialise();
        window = std::make_unique<MainWindow> (getApplicationName(), *controller);

        // --smoke-test <seconds>: launch, run, and quit (used by automated checks).
        if (commandLine.contains ("--smoke-test"))
        {
            const int secs = juce::jmax (1, commandLine.fromFirstOccurrenceOf ("--smoke-test", false, false).trim().getIntValue());
            juce::Timer::callAfterDelay (secs * 1000, [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
        }
    }

    void shutdown() override
    {
        window.reset();
        if (controller != nullptr)
            controller->shutdown();
        controller.reset();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr)
            window->toFront (true);
    }

private:
    std::unique_ptr<AppController> controller;
    std::unique_ptr<MainWindow> window;
};
} // namespace af

START_JUCE_APPLICATION (af::AmpForgeApplication)
