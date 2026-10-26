// SPDX-License-Identifier: LicenseRef-KYL-Noncommercial-1.0
#ifndef ARI_IME_CANDIDATE_PRESENTATION_H
#define ARI_IME_CANDIDATE_PRESENTATION_H

#include <string>
#include <vector>
#include "unicode.h"

namespace ari_ime {
// Presentation only: original strings remain the authority for selection and
// learning. A horizontal row gets padding only when the entire row is short.
inline std::vector<std::string>
candidateDisplayTexts(const std::vector<std::string> &candidates, bool horizontal) {
    auto displayed = candidates;
    if (horizontal) {
        int length = 0;
        for (const auto &text : candidates) {
            length += unicode::graphemeCount(text);
            if (length > 4) {
                return displayed;
            }
        }
        if (!displayed.empty() && length > 0) {
            displayed.back() += "\u3000";
        }
    } else {
        for (auto &text : displayed) {
            const int length = unicode::graphemeCount(text);
            if (length > 0 && length <= 4) {
                text += "\u3000";
            }
        }
    }
    return displayed;
}
} // namespace ari_ime
#endif
