#include "core/import/hash.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

Result<QString> hashFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error::io(QStringLiteral("Could not open %1: %2").arg(path, file.errorString()));

    const qint64 len = QFileInfo(path).size();
    const QByteArray prefix = file.read(HASH_PREFIX_BYTES);

    QCryptographicHash hasher(QCryptographicHash::Sha256);

    // Eight little-endian bytes, spelled out by hand rather than through
    // QtEndian so the encoding is obvious at the call site: this is the
    // one place a truncated file's length is folded into its identity, and
    // it must stay little-endian and eight bytes wide to match the Rust
    // build's hashes bit for bit.
    quint64 length = static_cast<quint64>(len);
    char lengthBytes[8];
    for (int i = 0; i < 8; ++i) {
        lengthBytes[i] = static_cast<char>(length & 0xff);
        length >>= 8;
    }
    hasher.addData(lengthBytes, sizeof(lengthBytes));
    hasher.addData(prefix);

    return Result<QString>::ok(QString::fromLatin1(hasher.result().toHex()));
}
