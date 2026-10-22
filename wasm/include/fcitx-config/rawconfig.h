// SPDX-License-Identifier: LicenseRef-KYL-Noncommercial-1.0
#ifndef ARI_WASM_FCITX_CONFIG_RAWCONFIG_H
#define ARI_WASM_FCITX_CONFIG_RAWCONFIG_H

namespace fcitx {
class RawConfig {
public:
    template <typename T>
    void setValueByPath(const char *, const T &) {}
};
}

#endif // ARI_WASM_FCITX_CONFIG_RAWCONFIG_H
