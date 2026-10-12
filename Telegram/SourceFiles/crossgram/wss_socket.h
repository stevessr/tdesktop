#pragma once

#include "crossgram/wss_transport.h"
#include "mtproto/details/mtproto_abstract_socket.h"

namespace MTP::details {

class WssSocket final : public AbstractSocket {
public:
	WssSocket(not_null<QThread*> thread, const QUrl &url);
	~WssSocket() override;

	void connectToHost(const QString &address, int port) override;
	[[nodiscard]] bool isGoodStartNonce(bytes::const_span nonce) override;
	void timedOut() override;
	[[nodiscard]] bool isConnected() override;
	[[nodiscard]] bool hasBytesAvailable() override;
	int64 read(bytes::span buffer) override;
	void write(bytes::const_span prefix, bytes::const_span buffer) override;
	int32 debugState() override;
	[[nodiscard]] QString debugPostfix() const override;

private:
	Crossgram::WssTransport _transport;
	QUrl _url;
	bool _notifying = false;
};

} // namespace MTP::details
