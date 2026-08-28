// Stripping the executable parts out of a book before it is rendered.
//
// foliate parses each chapter, rewrites its resource references, serialises it
// and hands the result to an iframe as a blob: URL -- with every <script>
// still in it ("TODO: replace inline scripts? probably not worth the trouble",
// foliate-js/epub.js). That iframe is sandboxed `allow-same-origin
// allow-scripts`, and Reader.qml has to set `localContentCanAccessFileUrls`
// so the page can read the book off disk, which makes the chapter same-origin
// with every file:// document on the machine. Measured on a two-kilobyte test
// EPUB, a chapter's own <script> could read /etc/hostname with fetch() and
// reach parent.qt.webChannelTransport -- the channel the ReaderBridge is on.
//
// The fix lives here rather than in foliate-js because foliate-js is a git
// clone and is gitignored, not a vendored tree: a `git pull` must not quietly
// undo it. That is the same reason js-polyfills.js exists.
//
// The iframe's sandbox attribute is deliberately left alone. `allow-scripts`
// is there for a WebKit bug that otherwise costs the renderer its events, and
// removing `allow-same-origin` would put the chapter in an opaque origin where
// foliate can no longer reach into contentDocument at all -- which is the
// whole renderer.

const XHTML_NS = 'http://www.w3.org/1999/xhtml'

// The media types the renderer parses and executes. Everything else -- images,
// fonts, CSS, PDF pages -- passes through untouched.
const MARKUP_TYPES = ['application/xhtml+xml', 'text/html', 'image/svg+xml']

/// Whether a manifest item's media type is markup we have to look inside.
export const isMarkupType = (type) =>
    MARKUP_TYPES.includes(String(type ?? '').split(';')[0].trim().toLowerCase())

// Attributes whose value is a URL the browser will follow. A `javascript:`
// value in any of them is script, with no <script> element to remove.
const URL_ATTRIBUTES = new Set([
    'href', 'src', 'action', 'formaction', 'data', 'poster', 'background',
])

// Whitespace and C0 controls are stripped throughout before the scheme is
// read, because that is what a browser does with them: a tab inside
// "java<TAB>script:" still runs, and a trim() alone would not see it.
const isJavascriptUrl = (value) =>
    /^javascript:/i.test(String(value ?? '').replace(/[\u0000-\u0020]+/g, ''))

/// Strip everything executable out of a parsed document, in place.
///
/// The document must be inert -- one from DOMParser, never a live one -- so
/// that nothing has run by the time this is called.
///
/// Returns what it removed, so the caller can log a book that tried.
export function sanitizeDocument(doc) {
    const removed = { scripts: 0, handlers: 0, urls: 0 }
    if (!doc?.documentElement)
        return removed

    // Wildcard namespace, because a chapter can carry <script> as XHTML and
    // again inside inline <svg>, and those are two different namespaces.
    // getElementsByTagNameNS returns a live list, so take a copy first.
    for (const el of [...doc.getElementsByTagNameNS('*', 'script')]) {
        el.remove()
        removed.scripts++
    }

    for (const el of doc.querySelectorAll('*')) {
        // getAttributeNames gives qualified names ("xlink:href"), which is
        // what removeAttribute matches on. Copy it: removing while iterating
        // the live NamedNodeMap skips entries.
        for (const name of [...el.getAttributeNames()]) {
            if (/^on/i.test(name)) {
                el.removeAttribute(name)
                removed.handlers++
                continue
            }
            // Compare on the local name so xlink:href is caught alongside href.
            const local = name.includes(':') ? name.split(':').pop() : name
            if (!URL_ATTRIBUTES.has(local.toLowerCase()))
                continue
            if (!isJavascriptUrl(el.getAttribute(name)))
                continue
            // Removed rather than rewritten to '#'. setAttribute with a
            // colon in the name throws InvalidCharacterError in an XML
            // document, and a src="#" would send the renderer after a
            // resource that is not there; a javascript: URL has no rendering
            // role to preserve.
            el.removeAttribute(name)
            removed.urls++
        }
    }

    injectPolicy(doc)
    return removed
}

// Belt and braces: even if a form of script survived the pass above, the
// document says it may not run any. An SVG document has no <head> to put it
// in, and the stripping above is what actually protects it.
function injectPolicy(doc) {
    const root = doc.documentElement
    if (root.localName !== 'html')
        return false

    const ns = root.namespaceURI || XHTML_NS
    let head = doc.getElementsByTagNameNS('*', 'head')[0]
    if (!head) {
        head = doc.createElementNS(ns, 'head')
        root.insertBefore(head, root.firstChild)
    }

    const meta = doc.createElementNS(ns, 'meta')
    meta.setAttribute('http-equiv', 'Content-Security-Policy')
    meta.setAttribute('content', "script-src 'none'")
    head.insertBefore(meta, head.firstChild)
    return true
}

/// Parse a chapter, sanitise it, and serialise it back.
///
/// The parse is what makes this safe to do with a parser rather than a regex:
/// a DOMParser document has no browsing context, so nothing in it runs, and we
/// see the markup the way the renderer will rather than the way it is written.
export function sanitizeMarkup(str, mediaType) {
    const type = isMarkupType(mediaType) ? String(mediaType).split(';')[0].trim() : 'text/html'
    const parser = new DOMParser()

    let doc = parser.parseFromString(str, type)
    if (!doc?.documentElement || doc.querySelector('parsererror')) {
        // foliate already retries invalid XHTML as HTML before it gets here,
        // so this is only reachable if its own serialisation round-trips
        // badly. Sanitising something is still better than passing it through.
        console.warn('omabook: chapter did not re-parse as ' + type + '; treating it as HTML')
        doc = parser.parseFromString(str, 'text/html')
    }

    sanitizeDocument(doc)
    // XMLSerializer for all three types, which is what foliate itself uses
    // when it writes the chapter back out (epub.js loadReplaced).
    return new XMLSerializer().serializeToString(doc)
}

/// Put a book behind the sanitiser. Call it after makeBook and before the book
/// is handed to the view.
///
/// This uses foliate's own extension point rather than wrapping
/// `section.load()`: `book.transformTarget` fires a `load` event before each
/// manifest item is fetched and a `data` event on the way to the blob the
/// iframe is pointed at. Everything the renderer will execute passes through
/// those two, for reflowable and fixed-layout EPUB and for MOBI alike, and the
/// blob URLs stay foliate's own -- so its cache, refcounting and
/// revokeObjectURL keep working exactly as they did. The paginator already
/// hooks the same `data` event to rewrite CSS.
///
/// Returns false for a book with no resource loader -- a PDF or a CBZ, which
/// has no markup to strip.
export function guardBook(book) {
    const target = book?.transformTarget
    if (!target)
        return false

    // An external script file is never fetched at all: loadItem asks first,
    // and a `false` here makes it return null (epub.js Loader.loadItem). The
    // <script src> element referring to it is removed by the pass below
    // regardless, so this only saves the read.
    target.addEventListener('load', ({ detail }) => {
        if (detail?.isScript) detail.allow = false
    })

    target.addEventListener('data', ({ detail }) => {
        if (!isMarkupType(detail?.type))
            return
        const type = detail.type
        // detail.data may be a string or a promise for one, and may be a
        // rejected promise for a chapter that failed to load; chaining keeps
        // that failure heading where it was already going.
        detail.data = Promise.resolve(detail.data)
            .then(data => sanitizeMarkup(String(data), type))
    })

    return true
}
