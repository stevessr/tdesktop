#pragma once

#include <QtCore/QString>
#include <cstddef>

inline QString operator""_q(const char16_t *data, std::size_t length) {
	return QString::fromUtf16(data, qsizetype(length));
}
