#pragma once

#include <QtTest>

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/db/database.h"
#include "core/db/migrations.h"
#include "core/models/book.h"
#include "core/omarchy.h"
#include "core/repo/bookrepository.h"
#include "core/result.h"

class CoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // --- db/migrations -----------------------------------------------

    void opensAndMigratesInMemory() {
        auto db = Database::openForTest();
        QSqlQuery query(db->connection());
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toLongLong(), SCHEMA_VERSION);
    }

    void migratingTwiceIsANoOp() {
        // openForTest() already migrated once on open; migrating again
        // explicitly must be a no-op, not a re-application or an error.
        auto db = Database::openForTest();
        const VoidResult second = migrate(db->connection());
        QVERIFY(second.isOk());

        QSqlQuery query(db->connection());
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toLongLong(), SCHEMA_VERSION);
    }

    void refusesToDowngrade() {
        auto db = Database::openForTest();
        QSqlQuery bump(db->connection());
        QVERIFY(bump.exec(QStringLiteral("PRAGMA user_version = %1").arg(SCHEMA_VERSION + 5)));

        const VoidResult result = migrate(db->connection());
        QVERIFY(result.isErr());
    }

    void foreignKeysAreEnforced() {
        auto db = Database::openForTest();
        QSqlQuery query(db->connection());
        QVERIFY(!query.exec(QStringLiteral("INSERT INTO book_tags (book_id, tag_id) VALUES (1, 1)")));
    }

    void splitsStatementsWithoutBreakingTriggers() {
        const QString sql = QStringLiteral(
            "CREATE TABLE t (id INTEGER);\n"
            "CREATE TRIGGER trg AFTER UPDATE ON t BEGIN\n"
            "  INSERT INTO t (id) VALUES (1);\n"
            "  INSERT INTO t (id) VALUES (2);\n"
            "END;\n"
            "CREATE TABLE u (id INTEGER);\n");

        const QStringList statements = splitStatements(sql);
        QCOMPARE(statements.size(), 3);
        QVERIFY(statements.at(1).contains(QStringLiteral("BEGIN")));
        QVERIFY(statements.at(1).contains(QStringLiteral("END")));
        // Both INSERTs stayed inside the trigger's single statement rather
        // than each becoming its own.
        QCOMPARE(statements.at(1).count(QStringLiteral("INSERT")), 2);
    }

    // --- db/database ---------------------------------------------------

    void vacuumIntoWritesAReadableSnapshot() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString sourcePath = dir.filePath(QStringLiteral("source.db"));
        auto source = Database::openForTest(sourcePath);

        QSqlQuery insert(source->connection());
        QVERIFY(insert.exec(QStringLiteral(
            "INSERT INTO categories (name, slug) VALUES ('Fiction', 'fiction')")));

        const QString snapshotPath = dir.filePath(QStringLiteral("snapshot.db"));
        const VoidResult vacuumed = source->vacuumInto(snapshotPath);
        QVERIFY(vacuumed.isOk());
        QVERIFY(QFileInfo::exists(snapshotPath));

        auto snapshot = Database::openForTest(snapshotPath);
        QSqlQuery check(snapshot->connection());
        QVERIFY(check.exec(QStringLiteral("SELECT slug FROM categories WHERE slug = 'fiction'")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toString(), QStringLiteral("fiction"));
    }

    // --- models/book -----------------------------------------------------

    void formatsRoundTripThroughStrings() {
        const BookFormat formats[] = {
            BookFormat::Epub, BookFormat::Pdf, BookFormat::Mobi, BookFormat::Azw3, BookFormat::Cbz,
        };
        for (BookFormat format : formats) {
            const Result<BookFormat> parsed = fromString<BookFormat>(toString(format));
            QVERIFY(parsed.isOk());
            QCOMPARE(parsed.value(), format);
        }
    }

    void onlyTheMobiFamilyNeedsAConverter() {
        QVERIFY(needsConversion(BookFormat::Mobi));
        QVERIFY(needsConversion(BookFormat::Azw3));
        QVERIFY(!needsConversion(BookFormat::Epub));
        QVERIFY(!needsConversion(BookFormat::Pdf));
    }

    void extensionsAreCaseInsensitiveAndDotTolerant() {
        const std::optional<BookFormat> epub = bookFormatFromExtension(QStringLiteral(".EPUB"));
        QVERIFY(epub.has_value());
        QCOMPARE(*epub, BookFormat::Epub);

        const std::optional<BookFormat> pdf = bookFormatFromExtension(QStringLiteral("pdf"));
        QVERIFY(pdf.has_value());
        QCOMPARE(*pdf, BookFormat::Pdf);

        QVERIFY(!bookFormatFromExtension(QStringLiteral("txt")).has_value());
    }

    void readablePathPrefersTheConvertedFile() {
        Book book;
        book.sourcePath = QStringLiteral("/books/a.mobi");
        book.format = BookFormat::Mobi;
        QCOMPARE(book.readablePath(), QStringLiteral("/books/a.mobi"));

        book.readingPath = QStringLiteral("/books/a.epub");
        QCOMPARE(book.readablePath(), QStringLiteral("/books/a.epub"));
    }

    void unknownStatusIsRejectedRatherThanDefaulted() {
        QVERIFY(fromString<BookStatus>(QStringLiteral("halfway")).isErr());
    }

    void authorsSurviveTheRoundTrip() {
        const QStringList authors = {
            QStringLiteral("Herman Melville"),
            QStringLiteral("Nathaniel Hawthorne"),
        };
        const QString json = Book::encodeAuthors(authors);
        QCOMPARE(Book::decodeAuthors(json), authors);

        Book book;
        QCOMPARE(book.authorLine(), QStringLiteral("Unknown author"));
        book.authors = authors;
        QCOMPARE(book.authorLine(), QStringLiteral("Herman Melville, Nathaniel Hawthorne"));
    }

    // --- omarchy ----------------------------------------------------

    void parsesTheSolitudePalette() {
        const auto values = Omarchy::parseSimpleToml(QStringLiteral(
            "\nmode = \"dark\"\n\naccent = \"#798186\"\n"
            "# a comment\nbackground = \"#101315\"\nforeground = \"#cacccc\"\n"));
        QCOMPARE(values.value(QStringLiteral("mode")), QStringLiteral("dark"));
        QCOMPARE(values.value(QStringLiteral("accent")), QStringLiteral("#798186"));
        QCOMPARE(values.value(QStringLiteral("foreground")), QStringLiteral("#cacccc"));
        QVERIFY(!values.contains(QStringLiteral("# a comment")));
    }

    void ignoresSectionHeadersAndJunk() {
        const auto values = Omarchy::parseSimpleToml(
            QStringLiteral("[section]\nnot a pair\nkey = \"v\"\nempty =\n"));
        QCOMPARE(values.size(), 1);
        QCOMPARE(values.value(QStringLiteral("key")), QStringLiteral("v"));
    }

    void readsTheCurrentOmarchyTheme() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString state = home.filePath(QStringLiteral("current"));
        QVERIFY(QDir().mkpath(state + QStringLiteral("/theme")));

        QFile colors(state + QStringLiteral("/theme/colors.toml"));
        QVERIFY(colors.open(QIODevice::WriteOnly | QIODevice::Text));
        colors.write("mode = \"light\"\naccent = \"#112233\"\nforeground = \"#101010\"\n");
        colors.close();

        QFile name(state + QStringLiteral("/theme.name"));
        QVERIFY(name.open(QIODevice::WriteOnly | QIODevice::Text));
        name.write("catppuccin\n");
        name.close();

        const OmarchyTheme theme = Omarchy::readFrom(state);
        QCOMPARE(theme.name, QStringLiteral("catppuccin"));
        QVERIFY(!theme.dark);
        QCOMPARE(theme.accent, QStringLiteral("#112233"));
        QCOMPARE(theme.foreground, QStringLiteral("#101010"));
        // Absent from the file, so the built-in default stands.
        QCOMPARE(theme.muted, QStringLiteral("#4b4e55"));
    }

    // Omarchy's "solitude" theme, verbatim -- the full palette, not just the
    // three keys the earlier tests happened to touch.
    void parsesTheFullSolitudePaletteFromARealFile() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString state = home.filePath(QStringLiteral("current"));
        QVERIFY(QDir().mkpath(state + QStringLiteral("/theme")));

        QFile colors(state + QStringLiteral("/theme/colors.toml"));
        QVERIFY(colors.open(QIODevice::WriteOnly | QIODevice::Text));
        colors.write(
            "mode = \"dark\"\n"
            "accent = \"#798186\"\n"
            "selection = \"#343d41\"\n"
            "muted = \"#4b4e55\"\n"
            "background = \"#101315\"\n"
            "dark_background = \"#0c0e10\"\n"
            "darker_background = \"#080a0b\"\n"
            "lighter_background = \"#101315\"\n"
            "foreground = \"#cacccc\"\n"
            "dark_foreground = \"#4b4e55\"\n"
            "bright_foreground = \"#a5aeb4\"\n");
        colors.close();

        const OmarchyTheme theme = Omarchy::readFrom(state);
        QVERIFY(theme.dark);
        QCOMPARE(theme.accent, QStringLiteral("#798186"));
        QCOMPARE(theme.selection, QStringLiteral("#343d41"));
        QCOMPARE(theme.muted, QStringLiteral("#4b4e55"));
        QCOMPARE(theme.background, QStringLiteral("#101315"));
        QCOMPARE(theme.darkBackground, QStringLiteral("#0c0e10"));
        QCOMPARE(theme.darkerBackground, QStringLiteral("#080a0b"));
        QCOMPARE(theme.lighterBackground, QStringLiteral("#101315"));
        QCOMPARE(theme.foreground, QStringLiteral("#cacccc"));
        QCOMPARE(theme.darkForeground, QStringLiteral("#4b4e55"));
        QCOMPARE(theme.brightForeground, QStringLiteral("#a5aeb4"));
    }

    // A key the file never mentions must come back as the built-in default,
    // not an empty string -- an empty QString is an invalid QML colour and
    // would render as black no matter what the theme actually says.
    void aMissingKeyFallsBackToItsDefaultNotAnEmptyString() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString state = home.filePath(QStringLiteral("current"));
        QVERIFY(QDir().mkpath(state + QStringLiteral("/theme")));

        QFile colors(state + QStringLiteral("/theme/colors.toml"));
        QVERIFY(colors.open(QIODevice::WriteOnly | QIODevice::Text));
        colors.write("mode = \"dark\"\naccent = \"#112233\"\n");
        colors.close();

        const OmarchyTheme theme = Omarchy::readFrom(state);
        const OmarchyTheme defaults;
        QCOMPARE(theme.accent, QStringLiteral("#112233"));
        QCOMPARE(theme.background, defaults.background);
        QCOMPARE(theme.darkBackground, defaults.darkBackground);
        QCOMPARE(theme.darkerBackground, defaults.darkerBackground);
        QCOMPARE(theme.lighterBackground, defaults.lighterBackground);
        QCOMPARE(theme.foreground, defaults.foreground);
        QCOMPARE(theme.darkForeground, defaults.darkForeground);
        QCOMPARE(theme.brightForeground, defaults.brightForeground);
        QCOMPARE(theme.selection, defaults.selection);
        QCOMPARE(theme.muted, defaults.muted);
        QVERIFY(!theme.background.isEmpty());
    }

    // No `mode` key at all: dark must be inferred from the background's
    // luminance rather than assumed, in both directions.
    void inferDarknessFromBackgroundLuminanceWhenModeIsAbsent() {
        QTemporaryDir darkHome;
        QVERIFY(darkHome.isValid());
        const QString darkState = darkHome.filePath(QStringLiteral("current"));
        QVERIFY(QDir().mkpath(darkState + QStringLiteral("/theme")));
        QFile darkColors(darkState + QStringLiteral("/theme/colors.toml"));
        QVERIFY(darkColors.open(QIODevice::WriteOnly | QIODevice::Text));
        // Solitude's own background, and no `mode` line at all.
        darkColors.write("background = \"#101315\"\n");
        darkColors.close();
        QVERIFY(Omarchy::readFrom(darkState).dark);

        QTemporaryDir lightHome;
        QVERIFY(lightHome.isValid());
        const QString lightState = lightHome.filePath(QStringLiteral("current"));
        QVERIFY(QDir().mkpath(lightState + QStringLiteral("/theme")));
        QFile lightColors(lightState + QStringLiteral("/theme/colors.toml"));
        QVERIFY(lightColors.open(QIODevice::WriteOnly | QIODevice::Text));
        // catppuccin-latte's background.
        lightColors.write("background = \"#eff1f5\"\n");
        lightColors.close();
        QVERIFY(!Omarchy::readFrom(lightState).dark);
    }

    void aDesktopWithoutOmarchyStillYieldsUsableColours() {
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        const OmarchyTheme theme = Omarchy::readFrom(empty.filePath(QStringLiteral("nothing")));
        QVERIFY(theme.accent.startsWith(QLatin1Char('#')));
        QVERIFY(theme.dark);
        QCOMPARE(theme.name, QStringLiteral("unknown"));
    }

    void aDesktopWithoutOmarchyIsNotAnError() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        OmarchyWatcher watcher;
        QVERIFY(!watcher.watch(root.filePath(QStringLiteral("absent"))));
    }

    // Replays what `omarchy-theme-set` actually does -- delete the theme
    // directory, move a new one into its place, then write theme.name -- and
    // checks the watcher survives it and reports it once. A watch on
    // current/theme rather than current/ passes this on the first round and
    // then goes silent forever, which is the bug this test exists to catch.
    void aThemeSwapWakesTheWatcherEveryTime() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString current = root.filePath(QStringLiteral("current"));
        QVERIFY(QDir().mkpath(current + QStringLiteral("/theme")));

        OmarchyWatcher watcher;
        QVERIFY(watcher.watch(current));
        QSignalSpy changes(&watcher, &OmarchyWatcher::themeChanged);

        for (int round = 0; round < 3; ++round) {
            const QString next = root.filePath(QStringLiteral("next"));
            QVERIFY(QDir().mkpath(next));
            QFile colors(next + QStringLiteral("/colors.toml"));
            QVERIFY(colors.open(QIODevice::WriteOnly | QIODevice::Text));
            colors.write(QStringLiteral("accent = \"#%1%1%1\"").arg(round).toUtf8());
            colors.close();

            QVERIFY(QDir(current + QStringLiteral("/theme")).removeRecursively());
            QVERIFY(QDir().rename(next, current + QStringLiteral("/theme")));
            QFile name(current + QStringLiteral("/theme.name"));
            QVERIFY(name.open(QIODevice::WriteOnly | QIODevice::Text));
            name.write(QStringLiteral("theme-%1").arg(round).toUtf8());
            name.close();

            QVERIFY2(changes.wait(5000), qPrintable(QStringLiteral("no notification for swap %1").arg(round)));
            // The burst is coalesced, so the whole swap is one wake-up, not
            // one per file touched.
            QCOMPARE(changes.count(), 1);
            changes.clear();
        }
    }

    // --- against a real library ---------------------------------------

    // A library already on disk has to keep opening as the schema moves, and
    // nothing proves that as directly as opening the real file.
    //
    // Skips rather than fails when there is no library on this machine: a
    // test may not depend on a file outside the repository.
    void opensTheRealLibraryDatabase() {
        const QString source = QDir::homePath()
            + QStringLiteral("/.local/share/omabook/omabook.db");
        if (!QFileInfo::exists(source))
            QSKIP("no existing omabook library on this machine");

        QTemporaryDir scratch;
        QVERIFY(scratch.isValid());
        const QString copy = scratch.filePath(QStringLiteral("omabook.db"));
        QVERIFY2(QFile::copy(source, copy), "could not copy the library");
        // The copy is read-write; a snapshot without its -wal is still
        // consistent because the app checkpoints on a clean exit.
        QVERIFY(QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner));

        auto db = Database::openForTest(copy);
        QVERIFY2(db != nullptr, "could not open the library database");

        QSqlQuery version(db->connection());
        QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(version.next());
        QCOMPARE(version.value(0).toLongLong(), SCHEMA_VERSION);

        // And the rows come back through the ported repository, not just the
        // raw file: enum decoding, the JSON authors column and the progress
        // join are all exercised by this one call.
        BookRepository books(db->connection());
        const auto listed = books.list(LibraryFilter::all(), BookSort::RecentlyAdded);
        QVERIFY(listed.isOk());
        QVERIFY(!listed.value().isEmpty());
        for (const Book &book : listed.value()) {
            QVERIFY(!book.title.isEmpty());
            QVERIFY(book.progress >= 0.0 && book.progress <= 1.0);
        }
    }
};
