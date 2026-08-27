// The pdf.js worker needs the same Map upsert methods as the main thread, and
// a worker inherits nothing from the page. So it starts here: apply the
// polyfill, then hand over to the real worker.
//
// pdf.js is pointed at this file through GlobalWorkerOptions.workerSrc, which
// keeps the vendored foliate-js tree untouched.
import "./js-polyfills.js";
import "./foliate-js/vendor/pdfjs/pdf.worker.mjs";
