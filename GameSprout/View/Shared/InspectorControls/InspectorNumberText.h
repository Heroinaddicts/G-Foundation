#pragma once
#include <QString>
#include <cmath>
#include <limits>

namespace InspectorNumbers {
// Choose the shortest significant-digit text that survives the Inspector's
// double parsing and float conversion. This hides binary tails without changing
// authored values, including at the finite float limits.
inline QString Format(float value) {
    QString best;
    for(int precision=1;precision<=std::numeric_limits<float>::max_digits10;++precision) {
        const QString text=QString::number(value,'g',precision);
        bool valid=false;const double parsed=text.toDouble(&valid);
        if(valid&&std::isfinite(parsed)&&std::fabs(parsed)<=std::numeric_limits<float>::max()&&static_cast<float>(parsed)==value&&
            (best.isEmpty()||text.size()<best.size()))best=text;
    }
    if(!best.isEmpty())return best;
    return QString::number(static_cast<double>(value),'g',std::numeric_limits<double>::max_digits10);
}
}
