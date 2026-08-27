#pragma once

#include <QtTest>

#include "core/tts/chunker.h"
#include "core/tts/kokoro.h"

class TtsTest : public QObject {
    Q_OBJECT

private slots:
    // --- tts/chunker ---------------------------------------------------

    void emptyInputYieldsNoChunks() {
        QVERIFY(Tts::chunk(QString()).isEmpty());
        QVERIFY(Tts::chunk(QStringLiteral("   \n\t ")).isEmpty());
    }

    void theFirstChunkIsSmallSoAudioStartsQuickly() {
        const QString sentence = QStringLiteral("This is a sentence of a reasonable length. ");
        QString page;
        for (int i = 0; i < 40; ++i)
            page += sentence;

        const QStringList chunks = Tts::chunk(page);
        QVERIFY(chunks.size() > 1);
        QVERIFY2(chunks.first().length() <= Tts::FIRST_CHUNK_CHARS,
                  qPrintable(QStringLiteral("first chunk was %1 chars").arg(chunks.first().length())));
    }

    void laterChunksMayUseTheLargerLimit() {
        const QString sentence = QStringLiteral("A short sentence here. ");
        QString page;
        for (int i = 0; i < 120; ++i)
            page += sentence;

        const QStringList chunks = Tts::chunk(page);
        QVERIFY(chunks.size() >= 3);

        bool exceededFirstChunkLimit = false;
        for (int i = 1; i < chunks.size(); ++i) {
            QVERIFY2(chunks.at(i).length() <= Tts::CHUNK_CHARS,
                      qPrintable(QStringLiteral("chunk too long: %1").arg(chunks.at(i).length())));
            if (chunks.at(i).length() > Tts::FIRST_CHUNK_CHARS)
                exceededFirstChunkLimit = true;
        }
        // Proves the two-tier scheme actually engages, not just that later
        // chunks stay under CHUNK_CHARS.
        QVERIFY(exceededFirstChunkLimit);
    }

    void textWithNoSentencePunctuationIsStillSplit() {
        QString page;
        for (int i = 0; i < 400; ++i)
            page += QStringLiteral("word ");

        const QStringList chunks = Tts::chunk(page);
        QVERIFY2(chunks.size() > 1, "a punctuation-free page must still chunk");
        QVERIFY(chunks.first().length() <= Tts::FIRST_CHUNK_CHARS);
    }

    void rejoiningTheChunksReproducesTheWords() {
        const QString page = QStringLiteral(
            "Call me Ishmael. Some years ago, never mind how long precisely, "
            "having little or no money in my purse, I thought I would sail about.");

        const QString rejoined = Tts::chunk(page).join(QLatin1Char(' '));
        const QStringList original =
            page.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const QStringList result =
            rejoined.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        QCOMPARE(result, original);
    }

    void whitespaceIsCollapsedSoSpeechDoesNotStutter() {
        const QStringList chunks = Tts::chunk(QStringLiteral("Line one\n\n   Line two\tstill going."));
        QCOMPARE(chunks, QStringList{QStringLiteral("Line one Line two still going.")});
    }

    void aSingleWordLongerThanTheLimitDoesNotLoopForever() {
        const QString page = QString(Tts::FIRST_CHUNK_CHARS * 3, QLatin1Char('x'));
        const QStringList chunks = Tts::chunk(page);
        QVERIFY(!chunks.isEmpty());
        QCOMPARE(chunks.join(QString()).length(), page.length());
    }

    void multibyteTextIsSplitOnCharacterNotByteBoundaries() {
        QString page;
        for (int i = 0; i < 200; ++i)
            page += QStringLiteral("महाभारत "); // "महाभारत "

        const QStringList chunks = Tts::chunk(page);
        QVERIFY(chunks.size() > 1);
        // The real assertion: none of this panicked on a byte index.
        for (const QString &c : chunks)
            QVERIFY(!c.isEmpty());
    }

    // --- tts/kokoro ------------------------------------------------------

    void voicesAreReadWhetherTheyArriveAsNamesOrAsObjects() {
        const QString names = QStringLiteral(R"({"voices":["af_heart","am_michael"]})");
        QCOMPARE(Kokoro::parseVoices(names),
                  QStringList({QStringLiteral("af_heart"), QStringLiteral("am_michael")}));

        // What kokoro-fastapi actually returns.
        const QString objects = QStringLiteral(
            R"({"voices":[{"id":"af_heart","name":"af_heart"},{"id":"am_michael","name":"am_michael"}]})");
        QCOMPARE(Kokoro::parseVoices(objects),
                  QStringList({QStringLiteral("af_heart"), QStringLiteral("am_michael")}));

        QVERIFY(Kokoro::parseVoices(QStringLiteral("{}")).isEmpty());
        QVERIFY(Kokoro::parseVoices(QStringLiteral(R"({"voices":"af_heart"})")).isEmpty());
    }

    void theSameTextAndVoiceCacheToTheSameFile() {
        QCOMPARE(Kokoro::stableId(QStringLiteral("hello"), QStringLiteral("am_michael")),
                  Kokoro::stableId(QStringLiteral("hello"), QStringLiteral("am_michael")));
    }

    void aDifferentVoiceIsADifferentFile() {
        QVERIFY(Kokoro::stableId(QStringLiteral("hello"), QStringLiteral("am_michael"))
                != Kokoro::stableId(QStringLiteral("hello"), QStringLiteral("af_heart")));
    }

    void trailingSlashesInTheUrlAreNormalised() {
        const Kokoro k(QStringLiteral("http://localhost:8880/"), QStringLiteral("v"), 1.0);
        QCOMPARE(k.baseUrl(), QStringLiteral("http://localhost:8880"));
    }

    void emptyTextIsRejectedBeforeAnyRequest() {
        // No server is running at this address; if the guard did not fire
        // first, this test would hang for the request's full timeout.
        Kokoro k(QStringLiteral("http://127.0.0.1:1"), QStringLiteral("v"), 1.0);
        const Result<QString> result = k.synthesize(QStringLiteral("   "));
        QVERIFY(result.isErr());
        QVERIFY(result.error().message.contains(QStringLiteral("nothing to synthesize")));
    }

    void anUnreachableServiceReportsUnavailableRatherThanHanging() {
        const Kokoro k(QStringLiteral("http://127.0.0.1:1"), QStringLiteral("v"), 1.0);
        QVERIFY(!k.available());
    }
};
