import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.omabook.app

// Settings: the choices that outlive a session.
//
// Loaded on demand rather than kept alive behind the library. Constructing the
// speech controller probes the synthesis service over HTTP, and an unreachable
// service costs its timeout — a price worth paying when someone opens this
// page, and not worth paying on every launch.
Item {
    id: view

    property var ai: null

    readonly property bool ready: view.ai !== null && view.ai !== undefined

    // Newline-separated from Rust, because a list of strings is not worth a
    // model type. See sidebar.rs for the same trade in JSON.
    readonly property var voiceList: tts.voices === "" ? [] : tts.voices.split("\n")

    // Five of Kokoro's sixty-eight voices. A dropdown is for choosing, not for
    // browsing, and nobody can pick between af_v0bella and af_bella from a
    // list. The ids are Kokoro's own; the labels say what they sound like.
    //
    // Offered even when the service is not up, so the choice can be made now
    // and be waiting when it is — the setting outlives the session either way.
    // Filtered to what the service has when it is answering, so a build that
    // ships fewer voices cannot leave a broken one selected, and whatever is
    // already chosen stays in the list even if it is not one of these.
    readonly property var voiceChoices: {
        var picks = [
            { id: "af_heart",   label: "Heart (American, female)" },
            { id: "af_bella",   label: "Bella (American, female)" },
            { id: "am_michael", label: "Michael (American, male)" },
            { id: "am_adam",    label: "Adam (American, male)" },
            { id: "bf_emma",    label: "Emma (British, female)" }
        ]
        var offered = view.voiceList
        var out = offered.length === 0
            ? picks
            : picks.filter(function(v) { return offered.indexOf(v.id) !== -1 })

        var current = tts.voice
        if (current !== "" && !out.some(function(v) { return v.id === current }))
            out.unshift({ id: current, label: current })
        return out
    }
    readonly property var localModels: view.ready && view.ai.local_models !== ""
        ? view.ai.local_models.split("\n") : []

    // Known Claude models, plus whatever is already configured — a model this
    // list has not heard of is still a legitimate choice.
    readonly property var remoteModels: {
        var known = ["claude-opus-5", "claude-sonnet-5", "claude-haiku-4-5-20251001"]
        var current = view.ready ? view.ai.remote_model : ""
        if (current !== "" && known.indexOf(current) === -1) known.unshift(current)
        return known
    }

    TtsController { id: tts }

    Component.onCompleted: {
        tts.refreshVoices()
        if (view.ready) {
            view.ai.refreshModels()
            view.ai.refresh()
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: view.width
            spacing: 0

            // ---------------------------------------------------------------
            SettingsHeading { text: "READING ALOUD" }

            SettingsRow {
                label: "Voice"
                hint: tts.engine === "kokoro"
                    ? "Applies to pages read from now on."
                    : "Kokoro is not running, so voices cannot be chosen yet."

                ComboBox {
                    id: voiceBox
                    Layout.preferredWidth: 260
                    model: view.voiceChoices
                    textRole: "label"
                    valueRole: "id"
                    currentIndex: {
                        for (var i = 0; i < view.voiceChoices.length; i++)
                            if (view.voiceChoices[i].id === tts.voice) return i
                        return 0
                    }
                    font.pixelSize: 12
                    onActivated: tts.changeVoice(currentValue)
                }
            }

            SettingsRow {
                label: "Speech service"
                hint: tts.engine === "kokoro"
                    ? ""
                    : "Without it, reading aloud falls back to the system speech "
                      + "engine, which is silent unless one is installed."

                Label {
                    text: tts.engine === "kokoro"
                        ? "Kokoro is running, voice " + tts.voice
                        : "not running"
                    color: tts.engine === "kokoro" ? Theme.text : Theme.muted
                    font.pixelSize: 12
                }

                FlatButton {
                    text: view.ready && view.ai.starting === "kokoro" ? "Starting…" : "Start"
                    visible: tts.engine !== "kokoro"
                    enabled: view.ready && view.ai.starting === ""
                    onClicked: view.ai.startKokoro()
                }

                FlatButton {
                    text: "Re-check"
                    onClicked: { tts.refreshEngine(); tts.refreshVoices() }
                }
            }

            // ---------------------------------------------------------------
            SettingsHeading { text: "ASSISTANT" }

            SettingsRow {
                label: "Local service"
                hint: view.ready && view.ai.available_providers.indexOf("local") !== -1
                    ? ""
                    : "Without Ollama, summaries and questions are unavailable "
                      + "unless a provider key is set below."

                Label {
                    text: view.ready && view.ai.available_providers.indexOf("local") !== -1
                        ? "Ollama is running"
                        : "not running"
                    color: view.ready && view.ai.available_providers.indexOf("local") !== -1
                        ? Theme.text : Theme.muted
                    font.pixelSize: 12
                }

                FlatButton {
                    text: view.ready && view.ai.starting === "ollama" ? "Starting…" : "Start"
                    visible: !(view.ready && view.ai.available_providers.indexOf("local") !== -1)
                    enabled: view.ready && view.ai.starting === ""
                    onClicked: view.ai.startOllama()
                }

                FlatButton {
                    text: "Re-check"
                    onClicked: view.ai.refresh()
                }
            }

            // Whatever the last start attempt had to say, success or not.
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.pad
                Layout.rightMargin: Theme.pad
                visible: view.ready && view.ai.service_message !== ""
                text: view.ready ? view.ai.service_message : ""
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 11
            }

            SettingsRow {
                label: "Local model"
                hint: view.localModels.length > 0
                    ? "Runs on this machine. Used for summaries, and for everything "
                      + "when no key is set."
                    : "Ollama is not answering, so there is nothing to choose from yet."

                ComboBox {
                    Layout.preferredWidth: 260
                    model: view.localModels
                    enabled: view.localModels.length > 0
                    currentIndex: view.ready
                        ? Math.max(0, view.localModels.indexOf(view.ai.local_model)) : 0
                    font.pixelSize: 12
                    onActivated: view.ai.setLocalModel(view.localModels[currentIndex])
                }
            }

            SettingsRow {
                label: "Remote model"
                hint: "Used for questions, which are open-ended enough to be worth a "
                      + "stronger model. Needs a key below."

                ComboBox {
                    Layout.preferredWidth: 260
                    model: view.remoteModels
                    currentIndex: view.ready
                        ? Math.max(0, view.remoteModels.indexOf(view.ai.remote_model)) : 0
                    font.pixelSize: 12
                    onActivated: view.ai.setRemoteModel(view.remoteModels[currentIndex])
                }
            }

            SettingsRow {
                label: "Provider key"
                hint: view.ready && view.ai.has_remote_key
                    ? "A key is set. It is stored in the library database, which is "
                      + "readable only by you."
                    : "Without a key, questions fall back to the local model. "
                      + "ANTHROPIC_API_KEY is used if it is exported."

                SearchBox {
                    id: keyField
                    Layout.preferredWidth: 260
                    placeholder: view.ready && view.ai.has_remote_key
                        ? "•••• stored, type to replace" : "sk-ant-…"
                    glyph: "⚿"
                    live: false
                    echoMode: TextInput.Password
                    onSubmitted: view.saveKey()
                }

                FlatButton {
                    text: "Save"
                    enabled: keyField.text.trim() !== ""
                    onClicked: view.saveKey()
                }

                FlatButton {
                    text: "Clear"
                    enabled: view.ready && view.ai.has_remote_key
                    tooltip: "Forget the stored key"
                    onClicked: { view.ai.setRemoteKey(""); keyField.text = "" }
                }
            }

            SettingsRow {
                label: "Reachable"
                Label {
                    text: !view.ready ? ""
                        : view.ai.available_providers === "none"
                          ? "nothing. Start Ollama, or set a key"
                          : view.ai.available_providers
                    color: Theme.muted
                    font.pixelSize: 12
                }
                FlatButton {
                    text: "Re-check"
                    onClicked: if (view.ready) { view.ai.refresh(); view.ai.refreshModels() }
                }
            }

            // ---------------------------------------------------------------
            SettingsHeading { text: "INDEXING" }

            SettingsRow {
                label: "Books"
                hint: "Questions about a book need it chunked and embedded first. "
                      + "Runs locally, about two minutes a book."

                FlatButton {
                    text: view.ready && view.ai.indexing ? "Stop" : "Index all books"
                    onClicked: {
                        if (!view.ready) return
                        if (view.ai.indexing) view.ai.cancelIndexing()
                        else {
                            view.ai.setBackgroundEnabled(true)
                            view.ai.indexLibrary()
                        }
                    }
                }

                BusyIndicator {
                    running: view.ready && view.ai.indexing
                    visible: running
                    implicitWidth: 18
                    implicitHeight: 18
                }

                Label {
                    Layout.fillWidth: true
                    text: !view.ready ? ""
                        : view.ai.indexing && view.ai.index_total > 0
                          ? view.ai.index_done + " / " + view.ai.index_total
                          : view.ai.status
                    color: Theme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            SettingsRow {
                label: "In the background"
                hint: "Background work never uses the remote provider, so it cannot "
                      + "cost money."

                CheckBox {
                    text: "Allowed"
                    checked: view.ready && view.ai.background_enabled
                    font.pixelSize: 12
                    onToggled: view.ai.setBackgroundEnabled(checked)
                }

                CheckBox {
                    text: "On battery too"
                    enabled: view.ready && view.ai.background_enabled
                    checked: view.ready && view.ai.background_on_battery
                    font.pixelSize: 12
                    onToggled: view.ai.setBackgroundOnBattery(checked)
                }
            }

            Item { Layout.preferredHeight: Theme.pad * 2 }
        }
    }

    function saveKey() {
        if (!view.ready || keyField.text.trim() === "") return
        view.ai.setRemoteKey(keyField.text)
        keyField.text = ""
    }
}
