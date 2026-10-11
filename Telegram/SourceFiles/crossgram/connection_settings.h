#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <vector>

namespace Crossgram {

struct ConnectionSettings {
	QUrl url;
	QByteArray rsaPublicKey;
	std::vector<int> dataCenters;
	QByteArray identity;
};

[[nodiscard]] bool LoadConnectionSettings(
	const QString &path,
	ConnectionSettings *result,
	QString *error);

} // namespace Crossgram
