#include "app_factory.h"
#include "include/base/cef_logging.h"
#include "include/cef_v8.h"
#include "process_messages.h"
#include "steam.h"

#include <unordered_map>

namespace {
constexpr cef_v8_propertyattribute_t kBridgePropertyAttributes = static_cast<cef_v8_propertyattribute_t>(V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTENUM | V8_PROPERTY_ATTRIBUTE_DONTDELETE);
constexpr char kJustCefBridgeName[] = "JustCefBridge";
constexpr char kJustCefDropBridgeName[] = "drop";

void SendSimpleMessage(CefRefPtr<CefFrame> frame, const char* messageName) {
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(messageName);
    frame->SendProcessMessage(PID_BROWSER, message);
}

class DropBridgeHandler : public CefV8Handler {
public:
    enum class Kind {
        DragStart,
        CommitPendingExternalDrop,
        CancelPendingExternalDrop,
    };

    explicit DropBridgeHandler(Kind kind) : kind_(kind) {}

    bool Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception) override {
        CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
        if (!context) {
            return true;
        }

        CefRefPtr<CefFrame> frame = context->GetFrame();
        if (!frame) {
            return true;
        }

        switch (kind_) {
            case Kind::DragStart: {
                CefRefPtr<CefV8Value> event = arguments.size() > 0 ? arguments[0] : nullptr;
                CefRefPtr<CefV8Value> isTrusted = event && event->IsObject() ? event->GetValue("isTrusted") : nullptr;
                CefRefPtr<CefV8Value> type = event && event->IsObject() ? event->GetValue("type") : nullptr;

                const bool isTrustedDragStart = isTrusted && isTrusted->IsBool() && isTrusted->GetBoolValue() && type && type->IsString() && type->GetStringValue().ToString() == "dragstart";
                if (!isTrustedDragStart) {
                    retval = CefV8Value::CreateBool(false);
                    return true;
                }

                SendSimpleMessage(frame, kDropLocalDragStartedMsg);
                retval = CefV8Value::CreateBool(true);
                return true;
            }
            case Kind::CommitPendingExternalDrop: {
                CefRefPtr<CefV8Value> event = arguments.size() > 0 ? arguments[0] : nullptr;
                CefRefPtr<CefV8Value> isTrusted = event && event->IsObject() ? event->GetValue("isTrusted") : nullptr;
                CefRefPtr<CefV8Value> type = event && event->IsObject() ? event->GetValue("type") : nullptr;
                CefRefPtr<CefV8Value> dataTransfer = event && event->IsObject() ? event->GetValue("dataTransfer") : nullptr;

                const bool isTrustedDrop = isTrusted && isTrusted->IsBool() && isTrusted->GetBoolValue() && type && type->IsString() && type->GetStringValue().ToString() == "drop" && dataTransfer && dataTransfer->IsObject();
                if (!isTrustedDrop) {
                    retval = CefV8Value::CreateBool(false);
                    return true;
                }

                LOG(INFO) << "Drop bridge commit requested from renderer.";
                SendSimpleMessage(frame, kDropBridgeCommitPendingExternalMsg);
                retval = CefV8Value::CreateBool(true);
                return true;
            }
            case Kind::CancelPendingExternalDrop:
                LOG(INFO) << "Drop bridge cancel requested from renderer.";
                SendSimpleMessage(frame, kDropBridgeCancelPendingExternalMsg);
                retval = CefV8Value::CreateBool(true);
                return true;
        }
        return true;
    }

private:
    const Kind kind_;

    IMPLEMENT_REFCOUNTING(DropBridgeHandler);
    DISALLOW_COPY_AND_ASSIGN(DropBridgeHandler);
};

CefRefPtr<CefV8Value> EnsureJustCefBridgeObject(CefRefPtr<CefV8Value> global) {
    CefRefPtr<CefV8Value> bridge = global->GetValue(kJustCefBridgeName);
    if (!bridge || !bridge->IsObject()) {
        bridge = CefV8Value::CreateObject(nullptr, nullptr);
        global->SetValue(kJustCefBridgeName, bridge, kBridgePropertyAttributes);
    }
    return bridge;
}

void InstallDropBridge(CefRefPtr<CefV8Context> context) {
    if (!context) {
        return;
    }

    CefRefPtr<CefV8Value> global = context->GetGlobal();
    if (!global) {
        return;
    }

    CefRefPtr<CefV8Value> addEventListener = global->GetValue("addEventListener");
    if (!addEventListener || !addEventListener->IsFunction()) {
        return;
    }

    CefV8ValueList listenerArgs;
    listenerArgs.push_back(CefV8Value::CreateString("dragstart"));
    listenerArgs.push_back(CefV8Value::CreateFunction("__justcefDropListener", new DropBridgeHandler(DropBridgeHandler::Kind::DragStart)));
    listenerArgs.push_back(CefV8Value::CreateBool(true));
    addEventListener->ExecuteFunction(global, listenerArgs);

    CefRefPtr<CefV8Value> bridge = EnsureJustCefBridgeObject(global);
    CefRefPtr<CefV8Value> drop = bridge->GetValue(kJustCefDropBridgeName);
    if (!drop || !drop->IsObject()) {
        drop = CefV8Value::CreateObject(nullptr, nullptr);
    }

    drop->SetValue("commitPendingExternalDrop", CefV8Value::CreateFunction("commitPendingExternalDrop", new DropBridgeHandler(DropBridgeHandler::Kind::CommitPendingExternalDrop)), kBridgePropertyAttributes);
    drop->SetValue("cancelPendingExternalDrop", CefV8Value::CreateFunction("cancelPendingExternalDrop", new DropBridgeHandler(DropBridgeHandler::Kind::CancelPendingExternalDrop)), kBridgePropertyAttributes);
    bridge->SetValue(kJustCefDropBridgeName, drop, kBridgePropertyAttributes);
}
}  // namespace

class RenderApp : public CefApp, public CefRenderProcessHandler {
public:
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

    void OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> extra_info) override {
        if (!browser) {
            return;
        }

        const bool enabled = extra_info && extra_info->GetBool("integratedDropBridgeEnabled");
        if (enabled) {
            drop_bridge_enabled_browsers_[browser->GetIdentifier()]++;
        }
    }

    void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override {
        if (!browser) {
            return;
        }

        auto it = drop_bridge_enabled_browsers_.find(browser->GetIdentifier());
        if (it == drop_bridge_enabled_browsers_.end()) {
            return;
        }

        if (--it->second <= 0) {
            drop_bridge_enabled_browsers_.erase(it);
        }
    }

    void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override {
        if (!browser || !context) {
            return;
        }

        if (drop_bridge_enabled_browsers_.find(browser->GetIdentifier()) != drop_bridge_enabled_browsers_.end()) {
            InstallDropBridge(context);
        }
    }

    void OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefDOMNode> node) override {
        const bool editable = node && node->IsEditable();
        if (!editable) {
            auto msg = CefProcessMessage::Create(kOskMsg);
            msg->GetArgumentList()->SetInt(0, 0);
            frame->SendProcessMessage(PID_BROWSER, msg);
            return;
        }

        int mode = k_EFloatingGamepadTextInputModeModeSingleLine;
        if (node->IsElement()) {
            auto tag = node->GetElementTagName().ToString();
            for (auto& c : tag) {
                c = (char)tolower(c);
            }
            if (tag == "textarea") {
                mode = k_EFloatingGamepadTextInputModeModeMultipleLines;
            } else if (tag == "input") {
                auto typ = node->GetElementAttribute("type").ToString();
                for (auto& c : typ) c = (char)tolower(c);
                if (typ == "email") {
                    mode = k_EFloatingGamepadTextInputModeModeEmail;
                } else if (typ == "number" || typ == "tel") { 
                    mode = k_EFloatingGamepadTextInputModeModeNumeric;
                }
            }
        }

        const CefRect r = node->GetElementBounds();
        auto msg = CefProcessMessage::Create(kOskMsg);
        auto args = msg->GetArgumentList();
        args->SetInt(0, 1);
        args->SetInt(1, r.x); args->SetInt(2, r.y);
        args->SetInt(3, r.width); args->SetInt(4, r.height);
        args->SetInt(5, mode);
        frame->SendProcessMessage(PID_BROWSER, msg);
    }

private:
    std::unordered_map<int, int> drop_bridge_enabled_browsers_;

    IMPLEMENT_REFCOUNTING(RenderApp);
};

namespace shared {
    CefRefPtr<CefApp> CreateRendererProcessApp() {
        return new RenderApp();
    }
}
