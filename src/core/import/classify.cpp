#include "core/import/classify.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace {

struct Shelf {
    QString name;
    QStringList keywords;
};

// The shelf names, and the words that put a book on each. Matching is
// case-insensitive on whole words, in this order, first hit wins -- so the
// specific shelves come before the broad ones they would otherwise fall
// into ("Science Fiction" before "Fiction", "Mathematics" before
// "Science"). The order is the algorithm.
const QVector<Shelf> &shelves() {
    static const QVector<Shelf> table = {
        { QStringLiteral("Science Fiction"),
          { QStringLiteral("science fiction"), QStringLiteral("sci-fi"), QStringLiteral("scifi"),
            QStringLiteral("space opera"), QStringLiteral("cyberpunk") } },
        { QStringLiteral("Fantasy"),
          { QStringLiteral("fantasy"), QStringLiteral("sword and sorcery"), QStringLiteral("epic fantasy") } },
        { QStringLiteral("Mystery"),
          { QStringLiteral("mystery"), QStringLiteral("detective"), QStringLiteral("crime"),
            QStringLiteral("thriller"), QStringLiteral("suspense"), QStringLiteral("noir") } },
        { QStringLiteral("Horror"),
          { QStringLiteral("horror"), QStringLiteral("ghost stories"), QStringLiteral("supernatural") } },
        { QStringLiteral("Romance"), { QStringLiteral("romance"), QStringLiteral("love stories") } },
        { QStringLiteral("Comics"),
          { QStringLiteral("comics"), QStringLiteral("graphic novel"), QStringLiteral("graphic novels"),
            QStringLiteral("manga") } },
        { QStringLiteral("Children"),
          { QStringLiteral("children"), QStringLiteral("juvenile"), QStringLiteral("picture books"),
            QStringLiteral("young adult"), QStringLiteral("ya") } },
        { QStringLiteral("Poetry"), { QStringLiteral("poetry"), QStringLiteral("poems"), QStringLiteral("verse") } },
        { QStringLiteral("Drama"),
          { QStringLiteral("drama"), QStringLiteral("plays"), QStringLiteral("theater"),
            QStringLiteral("theatre") } },
        { QStringLiteral("Fiction"),
          { QStringLiteral("fiction"), QStringLiteral("novel"), QStringLiteral("novels"),
            QStringLiteral("short stories"), QStringLiteral("literature"), QStringLiteral("literary") } },
        { QStringLiteral("Puzzles"),
          { QStringLiteral("puzzles"), QStringLiteral("riddles"), QStringLiteral("mathematical recreations"),
            QStringLiteral("brain teasers"), QStringLiteral("games") } },
        { QStringLiteral("Programming"),
          { QStringLiteral("programming"), QStringLiteral("computers"), QStringLiteral("computer science"),
            QStringLiteral("software"), QStringLiteral("software engineering"), QStringLiteral("coding"),
            QStringLiteral("algorithms"), QStringLiteral("rust"), QStringLiteral("python"),
            QStringLiteral("javascript"), QStringLiteral("typescript"), QStringLiteral("java"),
            QStringLiteral("ruby"), QStringLiteral("c++"), QStringLiteral("golang"), QStringLiteral("git"),
            QStringLiteral("version control"), QStringLiteral("machine learning"),
            QStringLiteral("deep learning"), QStringLiteral("neural networks"),
            QStringLiteral("artificial intelligence"), QStringLiteral("data science"),
            QStringLiteral("databases"), QStringLiteral("sql"), QStringLiteral("operating systems"),
            QStringLiteral("unix"), QStringLiteral("linux"), QStringLiteral("web development"),
            QStringLiteral("compilers"), QStringLiteral("cryptography"), QStringLiteral("crypto"),
            QStringLiteral("devops"), QStringLiteral("kubernetes"), QStringLiteral("docker"),
            QStringLiteral("testing"), QStringLiteral("language processing") } },
        { QStringLiteral("Mathematics"),
          { QStringLiteral("mathematics"), QStringLiteral("math"), QStringLiteral("maths"),
            QStringLiteral("calculus"), QStringLiteral("algebra"), QStringLiteral("linear algebra"),
            QStringLiteral("geometry"), QStringLiteral("statistics"), QStringLiteral("probability"),
            QStringLiteral("number theory"), QStringLiteral("topology"), QStringLiteral("logic"),
            QStringLiteral("optimization"), QStringLiteral("discrete mathematics"),
            QStringLiteral("category theory"), QStringLiteral("combinatorics"),
            QStringLiteral("trigonometry"), QStringLiteral("arithmetic"), QStringLiteral("formulas") } },
        { QStringLiteral("Science"),
          { QStringLiteral("science"), QStringLiteral("physics"), QStringLiteral("astronomy"),
            QStringLiteral("chemistry"), QStringLiteral("biology"), QStringLiteral("evolution"),
            QStringLiteral("geology"), QStringLiteral("cosmology"), QStringLiteral("relativity"),
            QStringLiteral("natural history"), QStringLiteral("nature"), QStringLiteral("engineering"),
            QStringLiteral("medicine") } },
        { QStringLiteral("History"),
          { QStringLiteral("history"), QStringLiteral("historical"), QStringLiteral("antiquity"),
            QStringLiteral("medieval"), QStringLiteral("war"), QStringLiteral("civilization") } },
        { QStringLiteral("Biography"),
          { QStringLiteral("biography"), QStringLiteral("autobiography"), QStringLiteral("memoir"),
            QStringLiteral("memoirs"), QStringLiteral("letters"), QStringLiteral("diaries") } },
        { QStringLiteral("Philosophy"),
          { QStringLiteral("philosophy"), QStringLiteral("ethics"), QStringLiteral("metaphysics"),
            QStringLiteral("epistemology"), QStringLiteral("stoicism") } },
        { QStringLiteral("Religion"),
          { QStringLiteral("religion"), QStringLiteral("theology"), QStringLiteral("bible"),
            QStringLiteral("spirituality"), QStringLiteral("buddhism"), QStringLiteral("christianity") } },
        { QStringLiteral("Psychology"),
          { QStringLiteral("psychology"), QStringLiteral("self-help"), QStringLiteral("self help"),
            QStringLiteral("mindfulness"), QStringLiteral("personal growth") } },
        { QStringLiteral("Business"),
          { QStringLiteral("business"), QStringLiteral("economics"), QStringLiteral("management"),
            QStringLiteral("finance"), QStringLiteral("investing"), QStringLiteral("marketing"),
            QStringLiteral("entrepreneurship") } },
        { QStringLiteral("Politics"),
          { QStringLiteral("politics"), QStringLiteral("political science"), QStringLiteral("government"),
            QStringLiteral("law"), QStringLiteral("sociology"), QStringLiteral("society") } },
        { QStringLiteral("Art"),
          { QStringLiteral("art"), QStringLiteral("design"), QStringLiteral("architecture"),
            QStringLiteral("photography"), QStringLiteral("music"), QStringLiteral("film") } },
        { QStringLiteral("Cooking"),
          { QStringLiteral("cooking"), QStringLiteral("cookbook"), QStringLiteral("recipes"),
            QStringLiteral("food"), QStringLiteral("wine") } },
        { QStringLiteral("Travel"),
          { QStringLiteral("travel"), QStringLiteral("voyages"), QStringLiteral("guidebook") } },
        { QStringLiteral("Reference"),
          { QStringLiteral("reference"), QStringLiteral("dictionary"), QStringLiteral("encyclopedia"),
            QStringLiteral("handbook"), QStringLiteral("manual"), QStringLiteral("textbook") } },
    };
    return table;
}

bool isAsciiWordChar(QChar c) {
    const char16_t u = c.unicode();
    return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z');
}

// Whether `needle` appears in `haystack` bounded by a non-alphanumeric
// character or the string edge on both sides, so "art" does not match
// inside "Martin" and "war" does not match inside "software".
bool containsWord(const QString &haystack, const QString &needle) {
    int from = 0;
    while (true) {
        const int at = haystack.indexOf(needle, from);
        if (at < 0)
            return false;

        const int end = at + needle.length();
        const bool beforeOk = at == 0 || !isAsciiWordChar(haystack.at(at - 1));
        const bool afterOk = end == haystack.length() || !isAsciiWordChar(haystack.at(end));
        if (beforeOk && afterOk)
            return true;

        from = at + 1;
    }
}

// The shelf whose vocabulary appears in `text`, on word boundaries.
std::optional<QString> shelfFor(const QString &text) {
    const QString lower = text.toLower();
    for (const Shelf &shelf : shelves()) {
        for (const QString &keyword : shelf.keywords) {
            if (containsWord(lower, keyword))
                return shelf.name;
        }
    }
    return std::nullopt;
}

// A subject head that can stand as a category on its own: a few words, no
// digits. Keeps out ISBN-like strings, "Fiction, English, 19th century"
// style catalogue tails, and a publisher's tagline pasted into the subject
// field.
bool looksLikeASubject(const QString &subjectHead) {
    const QStringList words =
        subjectHead.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.isEmpty() || words.size() > 3)
        return false;

    for (const QChar &c : subjectHead) {
        if (c.isDigit())
            return false;
    }
    return subjectHead.length() <= 40;
}

// "SCIENCE FICTION" and "science fiction" both become "Science Fiction",
// so a shelf that came from one publisher's shouting and another's
// whisper is still one shelf. Inner capitals in ordinary words are kept,
// so "Morphology (Animals)" is not flattened to "(animals)".
QString titleCase(const QString &text) {
    const QStringList words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList result;
    for (const QString &word : words) {
        if (word.isEmpty())
            continue;

        // "Shouted" means no lowercase letter anywhere in the word --
        // punctuation and digits do not disqualify it, so "D." in a name
        // still counts as shouted-or-not correctly for that check
        // elsewhere, and "PERPETUAL" here is recognised as shouted.
        bool shouted = true;
        for (const QChar &c : word) {
            if (c.isLetter() && !c.isUpper()) {
                shouted = false;
                break;
            }
        }

        const QString rest = shouted ? word.mid(1).toLower() : word.mid(1);
        result << (word.left(1).toUpper() + rest);
    }
    return result.join(QLatin1Char(' '));
}

} // namespace

QString head(const QString &subject) {
    const QString s = subject.trimmed();
    static const QStringList separators = {
        QStringLiteral(" / "),
        QStringLiteral(" -- "),
        QStringLiteral("--"),
        QStringLiteral("/"),
    };

    int cut = s.length();
    for (const QString &sep : separators) {
        const int at = s.indexOf(sep);
        if (at >= 0 && at < cut)
            cut = at;
    }

    QString result = s.left(cut).trimmed();
    while (result.endsWith(QLatin1Char(',')))
        result.chop(1);
    return result.trimmed();
}

bool looksLikeAName(const QString &text) {
    const QStringList words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.size() < 2 || words.size() > 3)
        return false;

    for (const QString &word : words) {
        if (word.isEmpty() || !word.at(0).isUpper())
            return false;

        // Every character -- punctuation included -- must be uppercase for
        // the word to count as fully shouted; a trailing period (as in
        // "D.") keeps an initial from being disqualified as a name.
        bool allUpper = true;
        for (const QChar &c : word) {
            if (!c.isUpper()) {
                allUpper = false;
                break;
            }
        }
        if (allUpper)
            return false;
    }
    return true;
}

std::optional<QString> categoryFor(const QString &folder, const FileMetadata &meta) {
    const QString trimmedFolder = folder.trimmed();
    if (!trimmedFolder.isEmpty())
        return trimmedFolder;

    // Whole subjects first, most specific shelf first: "FICTION / Science
    // Fiction / General" is science fiction, not fiction, and
    // "Mathematicians -- Great Britain -- Biography" is a biography. Only
    // then a head that names no shelf, as itself.
    const QString subjects = meta.subjects.join(QStringLiteral(" | "));
    const std::optional<QString> subjectShelf = shelfFor(subjects);
    if (subjectShelf.has_value())
        return subjectShelf;

    for (const QString &subject : meta.subjects) {
        const QString h = head(subject);
        if (looksLikeASubject(h) && !looksLikeAName(h))
            return titleCase(h);
    }

    QStringList proseParts;
    if (!meta.title.isEmpty())
        proseParts << meta.title;
    if (!meta.description.isEmpty())
        proseParts << meta.description;
    const QString prose = proseParts.join(QStringLiteral(" | "));

    return shelfFor(prose);
}
