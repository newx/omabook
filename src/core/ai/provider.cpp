#include "core/ai/provider.h"

bool isGenerative(TaskKind task) {
    return task != TaskKind::Embed;
}

ProviderClass prefers(TaskKind task) {
    switch (task) {
    case TaskKind::Embed:
    case TaskKind::Tag:
    case TaskKind::PageSummary:
        return ProviderClass::Local;
    case TaskKind::ChapterSummary:
    case TaskKind::Ask:
    case TaskKind::LibraryAsk:
        return ProviderClass::Remote;
    }
    return ProviderClass::Remote; // unreachable, silences -Wreturn-type
}

QString toString(TaskKind task) {
    switch (task) {
    case TaskKind::Embed:
        return QStringLiteral("embed");
    case TaskKind::Tag:
        return QStringLiteral("tag");
    case TaskKind::PageSummary:
        return QStringLiteral("page-summary");
    case TaskKind::ChapterSummary:
        return QStringLiteral("chapter-summary");
    case TaskKind::Ask:
        return QStringLiteral("ask");
    case TaskKind::LibraryAsk:
        return QStringLiteral("library-ask");
    }
    return QString();
}
