#pragma once

// The management surface of a transport (contract 2.1.0): what
// TransportInterface::describeManagement() answers with, and what an action
// answers through CoreFacade::completeAction().
//
// The shapes are the adapter action shapes, deliberately: phi-ui has one
// dialog for an action with a form and one modal for a result, and a
// transport's actions go through the same two. Only the route differs -
// `cmd.transport.action.invoke` instead of `cmd.adapter.action.invoke`.
//
// Management description (`describeManagement()`):
//
//     {
//       "summary": "3 clients, 2 sessions",       // one line for the card; optional
//       "actions": [
//         {
//           "id": "sessions",                      // required
//           "label": "Show sessions",              // shown on the button
//           "description": "...",                  // optional
//           "danger": false,                       // optional, a red button
//           "confirm": {"title": "...", "okText": "..."},   // optional, ask first
//           "hasForm": true,                       // optional; then `form` follows
//           "form": {"fields": [ ...adapter config fields... ]}
//         }
//       ]
//     }
//
// A form field carries `key`, `label`, `type` (string, integer, boolean,
// select, password), optional `default`, `placeholder`, `description` and
// `choices` ([{value,label}]) - the subset of the adapter config field that a
// dialog needs to draw an input.
//
// Action result (`completeAction()`):
//
//     {"status": 0, "resultValue": "Done."}                          // a sentence
//     {"status": 0, "resultValue": {"text": "...", "code": "...", "qr": "..."}}
//     {"status": 1, "error": {"message": "Why it failed"}}
//     {"status": 0, "reloadLayout": true}                            // re-list afterwards
//
// `status` is the phi command status: 0 Success, 1 Failure, 3 NotSupported,
// 4 InvalidArgument, 5 Busy, 7 NotAuthorized. `reloadLayout` asks the client to
// fetch the description again, for an action that changed what the card says.

#include "jsontext.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace phicore::transport {

namespace status {
inline constexpr int kSuccess = 0;
inline constexpr int kFailure = 1;
inline constexpr int kNotSupported = 3;
inline constexpr int kInvalidArgument = 4;
inline constexpr int kBusy = 5;
inline constexpr int kNotAuthorized = 7;
} // namespace status

/// One action of the description. `extraFieldsJson` is appended verbatim
/// inside the object (e.g. `"danger":true,"confirm":{...}`); empty for none.
[[nodiscard]] inline JsonText makeActionDescriptor(std::string_view id,
                                                   std::string_view label,
                                                   std::string_view description = {},
                                                   std::string_view extraFieldsJson = {})
{
    JsonText out("{\"id\":");
    out += jsonQuoted(id);
    if (!label.empty()) {
        out += ",\"label\":";
        out += jsonQuoted(label);
    }
    if (!description.empty()) {
        out += ",\"description\":";
        out += jsonQuoted(description);
    }
    if (!extraFieldsJson.empty()) {
        out += ',';
        out += extraFieldsJson;
    }
    out += '}';
    return out;
}

/// The whole description: a summary line and the actions, each built with
/// makeActionDescriptor() or by hand.
[[nodiscard]] inline JsonText makeManagementDescription(std::string_view summary,
                                                        std::initializer_list<std::string_view> actionsJson)
{
    JsonText out("{");
    bool first = true;
    if (!summary.empty()) {
        out += "\"summary\":";
        out += jsonQuoted(summary);
        first = false;
    }
    if (actionsJson.size() > 0) {
        if (!first)
            out += ',';
        out += "\"actions\":[";
        bool firstAction = true;
        for (const std::string_view action : actionsJson) {
            if (!firstAction)
                out += ',';
            firstAction = false;
            out += action;
        }
        out += ']';
    }
    out += '}';
    return out;
}

/// A successful result carrying a sentence.
[[nodiscard]] inline JsonText makeActionResultText(std::string_view text, bool reloadLayout = false)
{
    JsonText out("{\"status\":0,\"resultValue\":");
    out += jsonQuoted(text);
    if (reloadLayout)
        out += ",\"reloadLayout\":true";
    out += '}';
    return out;
}

/// A successful result carrying a value the client shows as it sees fit:
/// `resultValueJson` is any JSON value - a string, or `{text, code, qr}`.
[[nodiscard]] inline JsonText makeActionResultValue(std::string_view resultValueJson, bool reloadLayout = false)
{
    JsonText out("{\"status\":0,\"resultValue\":");
    out += resultValueJson.empty() ? std::string_view("null") : resultValueJson;
    if (reloadLayout)
        out += ",\"reloadLayout\":true";
    out += '}';
    return out;
}

/// A failed result. `statusCode` is one of the status:: values above.
[[nodiscard]] inline JsonText makeActionResultError(std::string_view message,
                                                    int statusCode = status::kFailure)
{
    JsonText out("{\"status\":");
    out += std::to_string(statusCode == status::kSuccess ? status::kFailure : statusCode);
    out += ",\"error\":{\"message\":";
    out += jsonQuoted(message);
    out += "}}";
    return out;
}

} // namespace phicore::transport
