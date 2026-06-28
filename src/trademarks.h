#pragma once

#include <QString>

namespace app {

// MathWorks trademark strings — see https://www.mathworks.com/company/trust-center/trademarks.html
inline QString matlabRegistered() {
    return QStringLiteral("MATLAB\u00AE");
}

inline QString mathWorksTrademarkNotice() {
    return QStringLiteral(
        "MATLAB and Simulink are registered trademarks of The MathWorks, Inc. "
        "See mathworks.com/trademarks for additional trademarks.");
}

}  // namespace app
