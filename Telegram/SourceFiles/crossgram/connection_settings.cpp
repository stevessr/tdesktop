#include "crossgram/connection_settings.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QHostAddress>

#include <set>

namespace Crossgram {
namespace {

bool Reject(QString *error, const QString &message) {
	if (error) *error = message;
	return false;
}

bool ValidDomain(const QString &host) {
	if (host.isEmpty() || host.size() > 253
		|| !QHostAddress(host).isNull()) {
		return false;
	}
	const auto ascii = QUrl::toAce(host);
	if (ascii.isEmpty() || !ascii.contains('.')) return false;
	for (const auto &part : ascii.split('.')) {
		if (part.isEmpty() || part.size() > 63
			|| part.front() == '-' || part.back() == '-') return false;
		for (const auto character : part) {
			const auto digit = character >= '0' && character <= '9';
			const auto letter = (character >= 'a' && character <= 'z')
				|| (character >= 'A' && character <= 'Z');
			if (!digit && !letter && character != '-') return false;
		}
	}
	return true;
}

} // namespace

bool LoadConnectionSettings(
		const QString &path,
		ConnectionSettings *result,
		QString *error) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return Reject(error, u"Missing CrossGram WSS configuration: "_q + path);
	}
	if (file.size() > 65536) {
		return Reject(error, u"CrossGram configuration exceeds 64 KiB."_q);
	}
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (!document.isObject()) {
		return Reject(error, u"CrossGram configuration must be JSON."_q);
	}
	const auto object = document.object();
	const auto value = object.value(u"url"_q);
	const auto rsa = object.value(u"rsa_public_key"_q);
	if (!value.isString() || !rsa.isString()) {
		return Reject(error, u"url and rsa_public_key are required strings."_q);
	}
	const auto urlText = value.toString();
	if (urlText.size() > 4096 || urlText.contains(u'\\')) {
		return Reject(error, u"Invalid CrossGram WSS URL."_q);
	}
	for (const auto character : urlText) {
		if (character.isSpace() || character.unicode() < 32
			|| character.unicode() == 127) {
			return Reject(error, u"Invalid CrossGram URL characters."_q);
		}
	}
	const auto url = QUrl(urlText, QUrl::StrictMode);
	if (!url.isValid() || url.scheme() != u"wss"
		|| url.userName().size() || url.password().size()
		|| url.authority().contains(u'@')
		|| url.hasFragment() || !url.path().startsWith(u'/')
		|| url.path().isEmpty() || url.port(443) < 1
		|| url.port(443) > 65535 || !ValidDomain(url.host())) {
		return Reject(error, u"Use a certificate-validated wss:// DNS URL with a path."_q);
	}
	auto pem = rsa.toString().toUtf8();
	if (pem.contains("PRIVATE KEY")
		|| (!pem.contains("BEGIN RSA PUBLIC KEY")
			&& !pem.contains("BEGIN PUBLIC KEY"))) {
		return Reject(error, u"Provide the server RSA public PEM, not a private key."_q);
	}
	auto ids = std::vector<int>();
	auto seen = std::set<int>();
	const auto dcList = object.value(u"dc_ids"_q);
	if (!dcList.isUndefined() && !dcList.isArray()) {
		return Reject(error, u"dc_ids must be an array."_q);
	}
	const auto list = dcList.isUndefined()
		? QJsonArray{ 1, 2, 3, 4, 5 }
		: dcList.toArray();
	for (const auto &entry : list) {
		if (!entry.isDouble()) return Reject(error, u"Invalid DC identifier."_q);
		const auto number = entry.toDouble();
		const auto id = int(number);
		if (number != id || id < 1 || id > 1000 || !seen.insert(id).second) {
			return Reject(error, u"Duplicate or invalid DC identifier."_q);
		}
		ids.push_back(id);
	}
	if (!seen.contains(2)) {
		return Reject(error, u"CrossGram must map initial DC 2."_q);
	}
	auto canonical = QByteArray();
	auto stream = QDataStream(&canonical, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << url.toString(QUrl::FullyEncoded) << pem;
	for (const auto id : seen) stream << qint32(id);
	*result = ConnectionSettings{
		url,
		std::move(pem),
		std::move(ids),
		QCryptographicHash::hash(canonical, QCryptographicHash::Sha256)
	};
	return true;
}

} // namespace Crossgram
