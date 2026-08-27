#pragma once

#include <QtTest>

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QVector>

#include "core/ai/anthropic.h"
#include "core/ai/assistant.h"
#include "core/ai/indexer.h"
#include "core/ai/ollama.h"
#include "core/ai/policy.h"
#include "core/ai/power.h"
#include "core/ai/prompts.h"
#include "core/ai/provider.h"
#include "core/ai/vectors.h"
#include "core/result.h"
#include "core/services.h"

class AiTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // --- ai/vectors ------------------------------------------------------

    void vectorsSurviveARoundTrip() {
        const QVector<float> original = { 0.0f, 1.5f, -2.25f, 1e-8f };
        const Result<QVector<float>> decoded = decode(encode(original));
        QVERIFY(decoded.isOk());
        QCOMPARE(decoded.value(), original);
    }

    void aTruncatedBlobIsAnErrorNotAShortVector() {
        QByteArray bytes(3, '\0');
        bytes[0] = 0;
        bytes[1] = 1;
        bytes[2] = 2;
        QVERIFY(decode(bytes).isErr());
    }

    void identicalVectorsScoreOne() {
        const QVector<float> v = { 1.0f, 2.0f, 3.0f };
        QVERIFY(qAbs(cosine(v, v) - 1.0f) < 1e-6f);
    }

    void oppositeVectorsScoreMinusOne() {
        const QVector<float> a = { 1.0f, 0.0f };
        const QVector<float> b = { -1.0f, 0.0f };
        QVERIFY(qAbs(cosine(a, b) + 1.0f) < 1e-6f);
    }

    void orthogonalVectorsScoreZero() {
        const QVector<float> a = { 1.0f, 0.0f };
        const QVector<float> b = { 0.0f, 1.0f };
        QVERIFY(qAbs(cosine(a, b)) < 1e-6f);
    }

    void magnitudeDoesNotAffectTheScore() {
        // Cosine is about direction; a longer chunk must not win for length.
        const QVector<float> a = { 1.0f, 2.0f, 3.0f };
        const QVector<float> b = { 10.0f, 20.0f, 30.0f };
        QVERIFY(qAbs(cosine(a, b) - 1.0f) < 1e-6f);
    }

    void mismatchedOrEmptyVectorsScoreZeroRatherThanPanicking() {
        const QVector<float> a2 = { 1.0f, 2.0f };
        const QVector<float> a1 = { 1.0f };
        QCOMPARE(cosine(a2, a1), 0.0f);
        QCOMPARE(cosine(QVector<float>(), QVector<float>()), 0.0f);
        const QVector<float> zero = { 0.0f, 0.0f };
        QCOMPARE(cosine(zero, zero), 0.0f);
    }

    void ftsTermsDropNoiseAndQuoteTheRest() {
        QCOMPARE(VectorStore::ftsTerms(QStringLiteral("What is the Pequod?")),
                  QStringLiteral("\"What\" OR \"the\" OR \"Pequod\""));
        // Punctuation must not reach FTS as operator syntax.
        QCOMPARE(VectorStore::ftsTerms(QStringLiteral("\"quoted\" (parens)")),
                  QStringLiteral("\"quoted\" OR \"parens\""));
        QCOMPARE(VectorStore::ftsTerms(QStringLiteral("a I of")), QString());
    }

    void normalisedRanksAreOrderedAndBounded() {
        const float strong = VectorStore::normaliseRank(-20.0);
        const float weak = VectorStore::normaliseRank(-1.0);
        QVERIFY2(strong > weak, "a better BM25 score must rank higher");
        QVERIFY(strong >= 0.0f && strong <= 1.0f);
        QVERIFY(weak >= 0.0f && weak <= 1.0f);
        QCOMPARE(VectorStore::normaliseRank(5.0), 0.0f);
    }

    // --- ai/power ----------------------------------------------------------

    void aMachineWithNoPowerSuppliesCountsAsMains() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(readFrom(dir.path()), Power::Mains);
        QCOMPARE(readFrom(QStringLiteral("/nonexistent-omabook-power-dir")), Power::Mains);
    }

    void aDischargingSystemBatteryIsBattery() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeSupply(dir.path(), QStringLiteral("BAT0"),
                    { { QStringLiteral("type"), QStringLiteral("Battery") },
                      { QStringLiteral("status"), QStringLiteral("Discharging") } });
        QCOMPARE(readFrom(dir.path()), Power::Battery);
    }

    void aChargingBatteryIsMains() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeSupply(dir.path(), QStringLiteral("BAT0"),
                    { { QStringLiteral("type"), QStringLiteral("Battery") },
                      { QStringLiteral("status"), QStringLiteral("Charging") } });
        QCOMPARE(readFrom(dir.path()), Power::Mains);
    }

    void anOnlineAdapterWinsOverADischargingBattery() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeSupply(dir.path(), QStringLiteral("AC"),
                    { { QStringLiteral("type"), QStringLiteral("Mains") },
                      { QStringLiteral("online"), QStringLiteral("1") } });
        writeSupply(dir.path(), QStringLiteral("BAT0"),
                    { { QStringLiteral("type"), QStringLiteral("Battery") },
                      { QStringLiteral("status"), QStringLiteral("Discharging") } });
        QCOMPARE(readFrom(dir.path()), Power::Mains);
    }

    void aWirelessKeyboardBatteryIsNotTheMachinesPowerSource() {
        // Exactly what a desktop reports: HID device batteries and no
        // system battery. Mistaking one for a laptop battery would stop
        // all background work on a desktop.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeSupply(dir.path(), QStringLiteral("hid-ABC-battery"),
                    { { QStringLiteral("type"), QStringLiteral("Battery") },
                      { QStringLiteral("status"), QStringLiteral("Discharging") },
                      { QStringLiteral("scope"), QStringLiteral("Device") } });
        QCOMPARE(readFrom(dir.path()), Power::Mains);
    }

    // --- ai/policy -----------------------------------------------------

    void backgroundWorkCanNeverReachARemoteProvider() {
        // The whole point: no unattended request can be billable.
        WorkPolicy on;
        on.backgroundEnabled = true;
        const TaskKind tasks[] = { TaskKind::Embed, TaskKind::Tag, TaskKind::PageSummary };
        for (TaskKind task : tasks) {
            const Decision decision = on.permits(task, Trigger::Background, ProviderClass::Remote, Power::Mains);
            QVERIFY2(!decision.isAllowed(), qPrintable(toString(task) + QStringLiteral(" escaped the rule")));
        }
    }

    void aUserWhoClicksMayUseARemoteProvider() {
        const WorkPolicy policy;
        const Decision decision = policy.permits(TaskKind::Ask, Trigger::Interactive, ProviderClass::Remote,
                                                   Power::Battery);
        QVERIFY2(decision.isAllowed(), "an explicit request is always honoured");
    }

    void summariesAreNeverPrecomputedEvenLocally() {
        WorkPolicy on;
        on.backgroundEnabled = true;
        const Decision decision =
            on.permits(TaskKind::ChapterSummary, Trigger::Background, ProviderClass::Local, Power::Mains);
        QVERIFY(!decision.isAllowed());
    }

    void indexingIsOffUntilAskedFor() {
        const WorkPolicy policy;
        const Decision decision = policy.permits(TaskKind::Embed, Trigger::Background, ProviderClass::Local,
                                                   Power::Mains);
        QVERIFY(!decision.isAllowed());
    }

    void indexingWaitsForMainsPower() {
        WorkPolicy on;
        on.backgroundEnabled = true;
        const Decision deferred = on.permits(TaskKind::Embed, Trigger::Background, ProviderClass::Local,
                                              Power::Battery);
        QCOMPARE(deferred.kind(), Decision::Kind::Defer);
        QCOMPARE(deferred.reason(), QStringLiteral("waiting for mains power"));

        // ...unless the user says otherwise.
        WorkPolicy eager = on;
        eager.backgroundOnBattery = true;
        QVERIFY(eager.permits(TaskKind::Embed, Trigger::Background, ProviderClass::Local, Power::Battery)
                    .isAllowed());
    }

    void localIndexingOnMainsIsTheCaseThatRuns() {
        WorkPolicy on;
        on.backgroundEnabled = true;
        QVERIFY(on.permits(TaskKind::Embed, Trigger::Background, ProviderClass::Local, Power::Mains).isAllowed());
    }

    void everyRefusalExplainsItself() {
        WorkPolicy on;
        on.backgroundEnabled = true;
        const Decision decision =
            on.permits(TaskKind::Embed, Trigger::Background, ProviderClass::Remote, Power::Mains);
        QVERIFY2(!decision.reason().isEmpty(), "a refusal the user cannot read is a bug");
    }

    // --- ai/prompts ------------------------------------------------------

    void aPageSummaryCarriesItsContext() {
        const QString prompt =
            Prompts::pageSummary(QStringLiteral("the text"), QStringLiteral("Moby-Dick"), QStringLiteral("Loomings"));
        QVERIFY(prompt.contains(QStringLiteral("Book: Moby-Dick")));
        QVERIFY(prompt.contains(QStringLiteral("Chapter: Loomings")));
        QVERIFY(prompt.contains(QStringLiteral("the text")));
    }

    void missingContextLeavesNoEmptyLabels() {
        const QString prompt = Prompts::pageSummary(QStringLiteral("the text"), QString(), QString());
        QVERIFY(!prompt.contains(QStringLiteral("Book:")));
        QVERIFY(!prompt.contains(QStringLiteral("Chapter:")));
    }

    void askingAboutABookForbidsGoingPastTheReader() {
        const QString flat =
            Prompts::askBook(QStringLiteral("who is Ishmael?"), QStringLiteral("some passages")).simplified();
        QVERIFY(flat.contains(QStringLiteral("not provide enough information")));
        QVERIFY2(flat.contains(QStringLiteral("may not have got")),
                  "the spoiler guard is the point of this scope");
    }

    void askingAboutTheLibraryForbidsInventingTitles() {
        const QString flat =
            Prompts::askLibrary(QStringLiteral("what maths books do I have?"), QStringLiteral("- A\n- B")).simplified();
        QVERIFY2(flat.contains(QStringLiteral("Do not invent titles")), qPrintable(flat));
        QVERIFY(flat.contains(QStringLiteral("what maths books do I have?")));
    }

    // --- ai/ollama -------------------------------------------------------

    void aThinkingPreambleIsRemoved() {
        QCOMPARE(Ollama::stripThinking(QStringLiteral("<think>hmm</think>\n\nThe answer.")),
                  QStringLiteral("The answer."));
    }

    void textWithoutAPreambleIsUntouched() {
        QCOMPARE(Ollama::stripThinking(QStringLiteral("The answer.")), QStringLiteral("The answer."));
    }

    void urlsAreNormalised() {
        Ollama ollama(QStringLiteral("http://localhost:11434/"), QStringLiteral("m"), QStringLiteral("e"));
        QCOMPARE(ollama.baseUrl(), QStringLiteral("http://localhost:11434"));
    }

    void embeddingEmptyTextFailsBeforeAnyRequest() {
        // Port to a closed local port so a bug that *did* send a request
        // would fail fast rather than hang the test.
        Ollama ollama(QStringLiteral("http://127.0.0.1:1"), QStringLiteral("m"), QStringLiteral("e"));
        QVERIFY(ollama.embed(QStringLiteral("   ")).isErr());
    }

    void anUnreachableServerIsReportedUnavailable() {
        Ollama ollama(QStringLiteral("http://127.0.0.1:1"), QStringLiteral("m"), QStringLiteral("e"));
        QVERIFY(!ollama.available());
    }

    void ollamaIsAlwaysLocal() {
        Ollama ollama(QStringLiteral("http://x"), QStringLiteral("m"), QStringLiteral("e"));
        QCOMPARE(ollama.providerClass(), ProviderClass::Local);
    }

    // --- ai/anthropic ----------------------------------------------------

    void textBlocksAreJoinedAndOthersIgnored() {
        QJsonObject thinking;
        thinking.insert(QStringLiteral("type"), QStringLiteral("thinking"));
        thinking.insert(QStringLiteral("thinking"), QString());

        QJsonObject partOne;
        partOne.insert(QStringLiteral("type"), QStringLiteral("text"));
        partOne.insert(QStringLiteral("text"), QStringLiteral("Part one. "));

        QJsonObject partTwo;
        partTwo.insert(QStringLiteral("type"), QStringLiteral("text"));
        partTwo.insert(QStringLiteral("text"), QStringLiteral("Part two."));

        QJsonArray content;
        content.append(thinking);
        content.append(partOne);
        content.append(partTwo);

        QJsonObject body;
        body.insert(QStringLiteral("content"), content);

        QCOMPARE(Anthropic::collectText(body), QStringLiteral("Part one. Part two."));
    }

    void aResponseWithNoTextYieldsAnEmptyStringNotAPanic() {
        QCOMPARE(Anthropic::collectText(QJsonObject()), QString());

        QJsonObject body;
        body.insert(QStringLiteral("content"), QJsonArray());
        QCOMPARE(Anthropic::collectText(body), QString());
    }

    void anthropicIsAlwaysRemote() {
        Anthropic anthropic(QStringLiteral("k"), QStringLiteral("m"));
        QCOMPARE(anthropic.providerClass(), ProviderClass::Remote);
    }

    void availabilityFollowsTheKeyAndMakesNoRequest() {
        Anthropic withKey(QStringLiteral("sk-test"), QStringLiteral("m"));
        QVERIFY(withKey.available());

        Anthropic blank(QStringLiteral("   "), QStringLiteral("m"));
        QVERIFY(!blank.available());
    }

    // --- ai/indexer --------------------------------------------------------

    void paragraphsArePackedUpToTheTarget() {
        const QString text =
                QStringLiteral("alpha ").repeated(100) + QStringLiteral("\n\n") + QStringLiteral("beta ").repeated(100);
        const QStringList chunks = Indexer::split(text);
        QVERIFY(!chunks.isEmpty());
        for (const QString &chunk : chunks)
            QVERIFY2(chunk.size() <= Indexer::TARGET_CHUNK_CHARS * 2, "chunk too long");
    }

    void aShortDocumentIsOneChunk() {
        const QStringList chunks =
                Indexer::split(QStringLiteral("A short paragraph that easily fits inside a single chunk of text."));
        QCOMPARE(chunks.size(), 1);
    }

    void emptyTextProducesNoChunks() {
        QVERIFY(Indexer::split(QString()).isEmpty());
        QVERIFY(Indexer::split(QStringLiteral("\n\n   \n\n")).isEmpty());
    }

    void noWordsAreLost() {
        const QString text = QStringLiteral("First paragraph here.\n\nSecond paragraph here.\n\nThird one.");
        const QString joined = Indexer::split(text).join(QLatin1Char(' '));
        const QStringList before = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const QStringList after = joined.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        QCOMPARE(after, before);
    }

    void aVeryLongParagraphIsBrokenUp() {
        const QString text = QStringLiteral("word ").repeated(2000);
        const QStringList chunks = Indexer::split(text);
        QVERIFY2(chunks.size() > 1, "one enormous paragraph must still be split");
    }

    void theSameMetadataHashesTheSame() {
        QCOMPARE(Indexer::shortHash(QStringLiteral("Moby-Dick. by Melville")),
                  Indexer::shortHash(QStringLiteral("Moby-Dick. by Melville")));
        QVERIFY(Indexer::shortHash(QStringLiteral("Moby-Dick")) != Indexer::shortHash(QStringLiteral("Billy Budd")));
    }

    // --- ai/assistant --------------------------------------------------------

    void anUnknownScopeDefaultsToTheSpoilerFreeOne() {
        QCOMPARE(Assistant::parseScope(QStringLiteral("so_far")), Scope::SoFar);
        QCOMPARE(Assistant::parseScope(QString()), Scope::SoFar);
        QCOMPARE(Assistant::parseScope(QStringLiteral("nonsense")), Scope::SoFar);
        QCOMPARE(Assistant::parseScope(QStringLiteral("book")), Scope::Book);
        QCOMPARE(Assistant::parseScope(QStringLiteral("page")), Scope::Page);
    }

    void excerptsAreFlattenedAndBounded() {
        const QString longText = QStringLiteral("word ").repeated(100);
        const QString shortened = Assistant::excerpt(longText);
        QVERIFY(shortened.size() <= 160);
        QVERIFY(shortened.endsWith(QStringLiteral("…")));
        QVERIFY(!shortened.contains(QLatin1Char('\n')));
    }

    void aShortPassageIsShownWhole() {
        QCOMPARE(Assistant::excerpt(QStringLiteral("Call me\n Ishmael.")), QStringLiteral("Call me Ishmael."));
    }

    // --- services ------------------------------------------------------------

    void aMissingToolIsReportedBeforeAnythingIsRun() {
        // The name is checked, not executed, so this makes no subprocess.
        QVERIFY(!Services::toolInstalled(QStringLiteral("definitely-not-a-real-command-xyz")));
    }

    void everyFailureSaysWhatToDo() {
        const StartFailure failures[] = { StartFailure::missing(QStringLiteral("docker")),
                                           StartFailure::missing(QStringLiteral("ollama")),
                                           StartFailure::failed(QStringLiteral("permission denied")),
                                           StartFailure::neverReady() };
        for (const StartFailure &failure : failures) {
            const QString message = failure.message();
            QVERIFY(!message.isEmpty());
            QVERIFY2(message.at(0).isUpper() || message.contains(QStringLiteral("permission")),
                      qPrintable(message));
        }
    }

    void errorsAreTrimmedButNotEmptied() {
        QCOMPARE(Services::firstSentence(QStringLiteral("Error: permission denied")),
                  QStringLiteral("permission denied"));
        QCOMPARE(Services::firstSentence(QStringLiteral("   ")), QStringLiteral("the command failed"));
        const QString longText = QStringLiteral("x").repeated(300);
        QVERIFY(Services::firstSentence(longText).size() <= 161);
    }

    void waitingGivesUpRatherThanHanging() {
        const std::optional<StartFailure> outcome = Services::waitUntil([]() { return false; }, 600);
        QVERIFY(outcome.has_value());
        QCOMPARE(outcome->kind(), StartFailureKind::NeverReady);
    }

    void waitingReturnsAsSoonAsItIsReady() {
        QElapsedTimer timer;
        timer.start();
        const std::optional<StartFailure> outcome = Services::waitUntil([]() { return true; }, 30000);
        QVERIFY(!outcome.has_value());
        QVERIFY(timer.elapsed() < 2000);
    }

private:
    // Builds a fake /sys/class/power_supply entry under `supplyDir`, so
    // ai/power's tests can drive readFrom() without touching the real
    // filesystem.
    static void writeSupply(const QString &supplyDir, const QString &name,
                             const QVector<QPair<QString, QString>> &fields) {
        QDir dir(supplyDir);
        QVERIFY2(dir.mkpath(name), "could not create fake power supply directory");
        const QString path = dir.filePath(name);
        for (const auto &field : fields) {
            QFile file(QDir(path).filePath(field.first));
            QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Text), "could not write fake power supply field");
            file.write(field.second.toUtf8());
        }
    }
};
