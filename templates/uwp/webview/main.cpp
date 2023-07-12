#include "pch.h"

extern "C" void        app_start(size_t count, ...);
extern "C" void        app_stop(size_t count, ...);
extern "C" const char* get_webserver_url();

namespace winrtWAA  = winrt::Windows::ApplicationModel::Activation;
namespace winrtWF   = winrt::Windows::Foundation;
namespace winrtWUX  = winrt::Windows::UI::Xaml;
namespace winrtWUXC = winrt::Windows::UI::Xaml::Controls;
namespace winrtWUC  = winrt::Windows::UI::Core;
namespace winrtWUP  = winrt::Windows::UI::Popups;

namespace winrtMUX  = winrt::Microsoft::UI::Xaml;
namespace winrtMUXC = winrt::Microsoft::UI::Xaml::Controls;

static void RaiseExceptionDialog(winrtWUC::CoreDispatcher dispatcher, winrt::hstring const& msg)
{
    dispatcher.RunIdleAsync([msg](auto args) {
        winrtWUP::MessageDialog dialog(msg);
        dialog.ShowAsync();
    });
}

struct App : winrtWUX::ApplicationT<App>, std::enable_shared_from_this<App>
{
    winrt::hstring GetRuntimeClassName() const { return winrt::hstring(L"App"); }

    void OnActivated(const winrtWAA::IActivatedEventArgs& args)
    {
        try
        {
            auto kind = args.Kind();
            if (kind == winrtWAA::ActivationKind::Protocol)
            {
                auto protocolArgs = args.as<winrtWAA::ProtocolActivatedEventArgs>();
                _Activate(protocolArgs.Uri());
            }
            else if (kind == winrtWAA::ActivationKind::CommandLineLaunch)
            {
                auto cmd = args.as<winrtWAA::CommandLineActivatedEventArgs>().Operation().Arguments();
                _Activate(winrtWF::Uri(cmd));
            }
            else
            {
                _Activate({nullptr});
            }
        } catch (std::exception const& ex)
        {
            RaiseExceptionDialog(_dispatcher, winrt::to_hstring(ex.what()));
        }
    }

    void OnLaunched(winrtWAA::LaunchActivatedEventArgs const& args)
    {
        auto arguments = args.Arguments();
        // auto info = args.TileActivatedInfo();
        // auto tileId = args.TileId();

        if (!arguments.empty())
        {
            auto uri = winrtWF::Uri(L"avid-launch:/" + (std::wstring)arguments);
            _Activate(uri);
        }
        else
        {
            _Activate({nullptr});
        }
    }

    void _Activate(winrtWF::Uri /*uri*/)
    {
        // If we have a phone contract, hide the status bar
        // if (winrtWF::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Phone.PhoneContract", 1, 0))
        {
            //    auto statusBar = winrt::Windows::UI::ViewManagement::StatGetForCurrentView();
            //    auto task = statusBar.HideAsync();
        }
        app_start(0);

        auto window = winrtWUX::Window::Current();
        window.Content(_webview);
        _webview.Source(winrtWF::Uri(winrt::to_hstring(get_webserver_url())));
        window.Activate();

        _dispatcher = window.Dispatcher();
    }

    App()           = default;
    ~App()          = default;
    App(App const&) = delete;
    App(App&&)      = delete;
    App& operator=(App&) = delete;
    App& operator=(App&&) = delete;

    private:
    winrtMUXC::WebView2      _webview;
    winrtWUC::CoreDispatcher _dispatcher{nullptr};
    winrt::event_token       _charrecieved;
    winrt::event_token       _sizechanged;
};

void AppMain()
{
    winrtWUX::Application::Start([](auto&&) { auto app = winrt::make<App>(); });
    app_stop(0);
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment();
    AppMain();
}