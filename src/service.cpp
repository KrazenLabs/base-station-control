#include "service.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "app_watch.hpp"
#include "automation.hpp"
#include "bluez.hpp"
#include "command_server.hpp"
#include "commands.hpp"
#include "common.hpp"
#include "controller.hpp"
#include "dbus_connection.hpp"
#include "power_events.hpp"
#include "scan_policy.hpp"
#include "settings.hpp"
#include "state_store.hpp"
#include "stations.hpp"
#include "status_report.hpp"
#include "steam_widget.hpp"
#include "switch_run.hpp"
#include "text.hpp"
#include "watched_apps.hpp"
#include "widget_bridge.hpp"
#include "widget_js.hpp"
#include "widget_model.hpp"
#include "widget_sync.hpp"

namespace bsc {

namespace {

constexpr double kRequestPollSeconds = 1.0;
constexpr double kRequestSafetySeconds = 5.0;
constexpr double kStatusMinimumGapSeconds = 1.0;
constexpr double kStatusHeartbeatSeconds = 4.0;
constexpr double kSaveDelaySeconds = 2.0;
constexpr double kLiveSaveSeconds = 300.0;
constexpr double kNoteSeconds = 20.0;
constexpr double kBusyWaitSeconds = 0.1;
constexpr double kIdleWaitSeconds = 1.0;

void debugLog(const std::string& message) {
    const long long millis = static_cast<long long>(monotonicSeconds() * 1000.0) % 100000;
    log("[debug +" + formatNumber(static_cast<double>(millis) / 1000.0, 3) + "] " + message);
}

class RequestWatch {
public:
    explicit RequestWatch(const std::filesystem::path& directory) : fd_(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {
        if (fd_ >= 0 && ::inotify_add_watch(fd_, directory.c_str(), IN_MOVED_TO | IN_CLOSE_WRITE) < 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    ~RequestWatch() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    RequestWatch(const RequestWatch&) = delete;
    RequestWatch& operator=(const RequestWatch&) = delete;

    int fd() const { return fd_; }

    bool takeChange() {
        char buffer[4096];
        bool changed = false;
        while (fd_ >= 0 && ::read(fd_, buffer, sizeof(buffer)) > 0) {
            changed = true;
        }
        return changed;
    }

private:
    int fd_ = -1;
};

ScanSettings scanSettingsFrom(const Settings& settings) {
    ScanSettings scan;
    scan.lingerSeconds = settings.scanLingerSeconds;
    scan.startupSeconds = settings.startupScanSeconds;
    scan.backgroundMinutes = settings.backgroundScanMinutes;
    scan.backgroundSeconds = settings.backgroundScanSeconds;
    return scan;
}

std::unique_ptr<WidgetSync> makeWidget(const Settings& settings) {
    if (!settings.steamBarWidget) {
        return nullptr;
    }
    const int port = settings.steamCefPort;
    return std::make_unique<WidgetSync>(
        [port](Logger widgetLog) { return std::make_unique<SteamWidget>(port, kWidgetScript, std::move(widgetLog)); });
}

class Service {
public:
    Service(const Paths& paths, std::string version)
        : paths_(paths),
          version_(std::move(version)),
          apps_(builtInApps()),
          settings_(loadSettings(paths.config, log)),
          loaded_(loadState(paths.state, apps_, log)),
          registry_(loaded_.state.stations),
          bluez_(bluezSettingsFrom(settings_), DBusConnection::openSystemBus, log,
                 settings_.debugLog ? Logger(debugLog) : Logger()),
          controller_(controllerSettingsFrom(settings_), bluez_, log),
          commands_(paths.requests, log),
          power_(DBusConnection::openSystemBus, log),
          scan_(scanSettingsFrom(settings_), monotonicSeconds()),
          options_(loaded_.state.automation),
          widget_(makeWidget(settings_)) {
        loaded_.state.stations.clear();
        bluez_.setAdvertHandler([this](const Advert& advert) { onAdvert(advert); });
    }

    int run();

private:
    void createFolders();
    void logStart();
    void onAdvert(const Advert& advert);
    ViewInputs viewInputs(double now) const;
    void setNote(const std::string& text, Tone tone);
    bool busy() const;
    void updateScanning(double now);
    void syncWidget(double now);
    void handleActions(const std::vector<WidgetAction>& actions, double now);
    void renameFromWidget(const WidgetAction& action, double now);
    void setOptionFromWidget(const WidgetAction& action, double now);
    void applyOptions();
    void markChanged(double now);
    void checkBoot(double now);
    int startRun(RunKind kind, double now, double budgetSeconds, const std::string& label);
    void stopRun(const std::string& reason, double now);
    void finishRun(double now);
    void releaseLockFor(RunKind kind);
    void handleAppEvent(const AppEvent& event, double now);
    std::string appsText() const;
    void handlePowerEvents(double now);
    void handleRequests(double now);
    void handleCommand(const IncomingCommand& command, double now);
    void handleOutcomes(double now);
    void deliverAnswers(double now);
    void writeStatus(double now, bool force);
    void saveIfDue(double now, bool force);
    void waitForEvents(double seconds);
    void shutdown();

    Paths paths_;
    std::string version_;
    AppList apps_;
    Settings settings_;
    LoadedState loaded_;
    StationRegistry registry_;
    BlueZ bluez_;
    Controller controller_;
    CommandServer commands_;
    PowerEvents power_;
    SwitchRun run_;
    AppWatcher appWatcher_;
    ScanPolicy scan_;
    AutomationOptions options_;
    std::unique_ptr<WidgetSync> widget_;
    std::unique_ptr<RequestWatch> requestWatch_;

    bool appsSeen_ = false;
    WidgetReport lastReport_;
    WidgetState widgetState_ = WidgetState::Waiting;
    std::string handledSession_;
    int handledSeq_ = 0;

    std::string note_;
    Tone noteTone_ = Tone::None;
    double noteUntil_ = 0.0;

    double lastRequestPoll_ = -1e18;
    std::string lastStatusBody_;
    double lastStatusWrite_ = -1e18;
    std::string statusError_;

    double lastSave_ = 0.0;
    std::optional<double> persistentDirtyAt_;
    bool liveDirty_ = false;
    bool refusedSave_ = false;

    int nextTag_ = 1;
};

void Service::onAdvert(const Advert& advert) {
    const std::optional<Sighting> sighting = sightingFrom(advert);
    if (!sighting) {
        return;
    }
    const bool known = registry_.find(sighting->name) != nullptr;
    const RegistryChange change = registry_.observe(*sighting, monotonicSeconds(), wallSeconds(), settings_.addNewStations);
    if (change == RegistryChange::Persistent) {
        markChanged(monotonicSeconds());
    } else if (change == RegistryChange::Live) {
        liveDirty_ = true;
    }
    if (!known && change != RegistryChange::None) {
        log("found Base Station " + sighting->name +
            (sighting->channel ? " on channel " + std::to_string(*sighting->channel) : std::string()));
    }
}

ViewInputs Service::viewInputs(double now) const {
    ViewInputs inputs;
    inputs.adapter = bluez_.adapterState();
    inputs.scanning = bluez_.scanning();
    inputs.scanningSince = scan_.scanningSince();
    inputs.now = now;
    inputs.missingAfterSeconds = settings_.missingAfterSeconds;
    if (now < noteUntil_) {
        inputs.note = note_;
        inputs.noteTone = noteTone_;
    }
    inputs.handledSession = handledSession_;
    inputs.handledSeq = handledSeq_;
    inputs.automation = options_;
    inputs.apps = apps_;
    inputs.sleepHookError = power_.status(PowerLock::Sleep).error;
    inputs.shutdownHookError = power_.status(PowerLock::Shutdown).error;
    inputs.installedApps = appWatcher_.installedApps();
    return inputs;
}

void Service::setNote(const std::string& text, Tone tone) {
    note_ = text;
    noteTone_ = tone;
    noteUntil_ = monotonicSeconds() + kNoteSeconds;
}

bool Service::busy() const {
    return bluez_.scanning() || controller_.busy() || run_.active() || lastReport_.visible || lastReport_.popupOpen ||
           power_.announcementPending();
}

void Service::updateScanning(double now) {
    ScanReasons reasons;
    reasons.menuOpen = lastReport_.visible || lastReport_.popupOpen;
    reasons.switching = controller_.busy();
    reasons.listening = run_.listening(now);
    bluez_.setScanning(scan_.wanted(reasons, now));
    scan_.observe(bluez_.scanning(), now);
}

void Service::syncWidget(double now) {
    if (!widget_) {
        return;
    }
    WidgetUpdate update = widget_->take();
    for (const std::string& message : update.logs) {
        log(message);
    }
    for (std::optional<WidgetReport>& result : update.results) {
        if (!result) {
            lastReport_ = WidgetReport{};
            continue;
        }
        lastReport_ = std::move(*result);
        handleActions(lastReport_.actions, now);
        if (lastReport_.appsReported) {
            appsSeen_ = true;
            for (const AppEvent& event : appWatcher_.update(lastReport_.apps, now)) {
                handleAppEvent(event, now);
            }
        }
    }
    widgetState_ = update.state;
    const bool active = lastReport_.visible || lastReport_.popupOpen || controller_.busy() || run_.active();
    widget_->publish(writeJson(widgetData(registry_, controller_, viewInputs(now))), active);
}

void Service::handleActions(const std::vector<WidgetAction>& actions, double now) {
    for (const WidgetAction& action : actions) {
        if (action.session != handledSession_) {
            handledSession_ = action.session;
            handledSeq_ = 0;
        }
        handledSeq_ = std::max(handledSeq_, action.seq);
        switch (action.kind) {
            case ActionKind::Set: {
                const Station* station = registry_.find(action.station);
                if (!station || station->record.ignored) {
                    break;
                }
                log("Steam bar: " + station->title() + (action.on ? " on" : " off"));
                controller_.request(registry_, station->name(), controller_.commandFor(action.on));
                break;
            }
            case ActionKind::AllOn:
            case ActionKind::AllOff: {
                const bool on = action.kind == ActionKind::AllOn;
                startRun(on ? RunKind::AllOn : RunKind::AllOff, now, 0.0, std::string("Steam bar: all ") + (on ? "on" : "off"));
                break;
            }
            case ActionKind::Rename: renameFromWidget(action, now); break;
            case ActionKind::Option: setOptionFromWidget(action, now); break;
            case ActionKind::BluetoothOn:
                log("Steam bar: enable Bluetooth");
                bluez_.powerOn([this](const std::string& error) {
                    if (!error.empty()) {
                        log("failed to enable Bluetooth: " + error);
                        setNote("Failed to activate Bluetooth: " + bluetoothFailureReason(error), Tone::Error);
                    }
                });
                break;
            case ActionKind::Unknown: break;
        }
    }
}

void Service::renameFromWidget(const WidgetAction& action, double now) {
    const Station* station = registry_.find(action.station);
    if (!station) {
        return;
    }
    const std::string before = station->title();
    const CommandResult result = renameStation(registry_, station->name(), trim(action.text));
    if (!result.ok) {
        log("Steam bar: could not rename " + before + ": " + result.message);
        setNote(capitalizeFirst(result.message), Tone::Error);
    } else if (result.stateChanged) {
        log("Steam bar: " + result.message);
        markChanged(now);
    }
}

void Service::setOptionFromWidget(const WidgetAction& action, double now) {
    const std::optional<OptionInfo> option = findOption(apps_, action.key);
    if (!option || optionValue(options_, option->id) == action.on) {
        return;
    }
    setOptionValue(options_, option->id, action.on);
    log("Steam bar: " + option->description + ": " + (action.on ? "on" : "off"));
    applyOptions();
    markChanged(now);
}

void Service::applyOptions() {
    power_.want(PowerLock::Sleep, options_.offAtSleep);
    power_.want(PowerLock::Shutdown, options_.offAtShutdown);
}

void Service::markChanged(double now) {
    if (!persistentDirtyAt_) {
        persistentDirtyAt_ = now;
    }
}

void Service::checkBoot(double now) {
    const std::string bootId = currentBootId();
    if (bootId.empty() || bootId == loaded_.state.bootId) {
        return;
    }
    const bool firstRun = loaded_.state.bootId.empty();
    loaded_.state.bootId = bootId;
    markChanged(now);
    saveIfDue(now, true);
    if (!firstRun && options_.onAtStartup) {
        startRun(RunKind::Startup, now, 0.0, "");
    }
}

void Service::releaseLockFor(RunKind kind) {
    if (kind == RunKind::Sleep) {
        power_.release(PowerLock::Sleep);
    } else if (kind == RunKind::Shutdown) {
        power_.release(PowerLock::Shutdown);
    }
}

int Service::startRun(RunKind kind, double now, double budgetSeconds, const std::string& label) {
    stopRun("replaced by: " + (label.empty() ? std::string(runText(kind)) : label), now);
    const int tag = nextTag_++;
    run_.start(kind, now, budgetSeconds, tag, label);
    finishRun(now);
    return tag;
}

void Service::stopRun(const std::string& reason, double now) {
    if (!run_.active()) {
        return;
    }
    run_.stop(controller_, reason, now);
    finishRun(now);
}

void Service::finishRun(double now) {
    for (const std::string& message : run_.takeMessages()) {
        log(message);
    }
    if (const std::optional<RunReport> report = run_.takeReport()) {
        releaseLockFor(report->kind);
        commands_.onRunReport(*report);
        deliverAnswers(now);
    }
}

void Service::handleAppEvent(const AppEvent& event, double now) {
    const WatchedApp* app = findApp(apps_, event.id);
    const std::string name = app ? app->name : "app " + std::to_string(event.id);
    const bool wanted = optionValue(options_, appOption(event.id, event.launched));
    if (event.launched) {
        if (wanted) {
            startRun(RunKind::AppLaunch, now, 0.0, name + " started");
        } else {
            log(name + " started");
        }
        return;
    }
    if (!wanted) {
        log(name + " quit");
    } else if (appWatcher_.anyOtherRunning(event.id)) {
        log(name + " quit, but another watched app is still running");
    } else {
        startRun(RunKind::AppQuit, now, 0.0, name + " quit");
    }
}

std::string Service::appsText() const {
    if (!appsSeen_) {
        return "";
    }
    std::vector<std::string> parts;
    for (const WatchedApp& app : apps_) {
        const char* state = appWatcher_.running(app.id)     ? " running"
                            : appWatcher_.installed(app.id) ? " installed, not running"
                                                            : " not installed";
        parts.push_back(app.name + state);
    }
    return joinWords(parts, "; ");
}

void Service::handlePowerEvents(double now) {
    for (const PowerEvent& event : power_.takeEvents()) {
        switch (event.kind) {
            case PowerEventKind::Sleeping:
                if (options_.offAtSleep) {
                    startRun(RunKind::Sleep, now, power_.delayBudgetSeconds(), "");
                } else {
                    log("headset going to sleep");
                    power_.release(PowerLock::Sleep);
                }
                break;
            case PowerEventKind::Resumed:
                if (run_.active() && !runTurnsOn(run_.kind())) {
                    stopRun("headset woke up", now);
                }
                if (options_.onAfterSleep) {
                    startRun(RunKind::Wake, now, 0.0, "");
                } else {
                    log("headset woke up");
                }
                break;
            case PowerEventKind::ShuttingDown:
                if (isRestartType(event.shutdownType)) {
                    log("headset restarting (" + event.shutdownType + ")");
                    power_.release(PowerLock::Shutdown);
                } else if (options_.offAtShutdown) {
                    startRun(RunKind::Shutdown, now, power_.delayBudgetSeconds(), "");
                } else {
                    log("headset shutting down");
                    power_.release(PowerLock::Shutdown);
                }
                break;
            case PowerEventKind::ShutdownCancelled:
                if (run_.active() && run_.kind() == RunKind::Shutdown) {
                    stopRun("shutdown was cancelled", now);
                }
                log("shutdown cancelled");
                break;
        }
    }
}

void Service::handleCommand(const IncomingCommand& command, double now) {
    const int tag = nextTag_++;
    log("command: " + joinWords(command.words, " "));
    const CommandResult result = executeCommand(CommandContext{registry_, controller_, options_, apps_}, command.words, tag);
    if (result.stateChanged || result.optionsChanged) {
        markChanged(now);
    }
    if (result.optionsChanged) {
        applyOptions();
    }
    if (result.scanSeconds > 0.0) {
        scan_.scanFor(result.scanSeconds, now);
    }
    if (result.powerOnBluetooth) {
        bluez_.powerOn([](const std::string& error) {
            log(error.empty() ? std::string("Bluetooth enabled (command)") : "failed to enable Bluetooth: " + error);
        });
    }
    if (result.groupSwitch) {
        const bool on = result.groupSwitch->on;
        const bool fromPanel = result.groupSwitch->style == AnswerStyle::PanelSummary;
        const std::string label = std::string(fromPanel ? "Community Apps tab: all " : "command: all ") + (on ? "on" : "off");
        const int runTag = startRun(on ? RunKind::AllOn : RunKind::AllOff, now, 0.0, label);
        commands_.waitForRun(command.requestId, runTag, result.groupSwitch->style, now);
        return;
    }
    if (!result.waitFor.empty()) {
        commands_.waitForStations(command.requestId, tag, result.waitFor, now);
        return;
    }
    commands_.answerNow(command.requestId, result.ok, result.message);
}

void Service::handleRequests(double now) {
    const bool watched = requestWatch_ && requestWatch_->fd() >= 0;
    const bool due = watched ? requestWatch_->takeChange() || now - lastRequestPoll_ >= kRequestSafetySeconds
                             : now - lastRequestPoll_ >= kRequestPollSeconds;
    if (!due) {
        return;
    }
    lastRequestPoll_ = now;
    for (const IncomingCommand& command : commands_.takeCommands()) {
        handleCommand(command, now);
    }
    commands_.cleanUp();
}

void Service::handleOutcomes(double now) {
    for (const Outcome& outcome : controller_.takeOutcomes()) {
        run_.addOutcome(outcome);
        commands_.onOutcome(outcome, registry_);
    }
    deliverAnswers(now);
}

void Service::deliverAnswers(double now) {
    if (commands_.hasReadyAnswers()) {
        writeStatus(now, true);
        commands_.sendReadyAnswers();
    }
}

void Service::writeStatus(double now, bool force) {
    if (!force && now - lastStatusWrite_ < kStatusMinimumGapSeconds) {
        return;
    }
    ServiceStatus service;
    service.version = version_;
    service.adapter = bluez_.adapterState();
    service.scanning = bluez_.scanning();
    service.widget = widget_ ? widgetState_ : WidgetState::Off;
    service.menuOpen = lastReport_.visible;
    service.popupOpen = lastReport_.popupOpen;
    service.automation = options_;
    service.apps = apps_;
    service.sleepHook = describeLock(power_.status(PowerLock::Sleep));
    service.shutdownHook = describeLock(power_.status(PowerLock::Shutdown));
    service.automationRun = run_.active() ? run_.label() : "";
    service.appsText = appsText();
    service.stateProblem = loaded_.writable() ? "" : loaded_.problem;
    const Json::Value status = statusJson(registry_, controller_, viewInputs(now), service, wallSeconds());
    Json::Value body = status;
    body.removeMember("updated");
    const std::string bodyText = writeJson(body);
    if (!force && bodyText == lastStatusBody_ && now - lastStatusWrite_ < kStatusHeartbeatSeconds) {
        return;
    }
    std::string error;
    if (!writeFileAtomically(paths_.status, writeJson(status, JsonLayout::Indented) + "\n", &error)) {
        if (error != statusError_) {
            statusError_ = error;
            log("cannot write status file (" + error + ")");
        }
        return;
    }
    if (!statusError_.empty()) {
        statusError_.clear();
        log("status file recovered");
    }
    lastStatusBody_ = bodyText;
    lastStatusWrite_ = now;
}

void Service::saveIfDue(double now, bool force) {
    const bool dirty = persistentDirtyAt_ || liveDirty_;
    const bool persistentDue = persistentDirtyAt_ && now - *persistentDirtyAt_ >= kSaveDelaySeconds;
    const bool liveDue = liveDirty_ && now - lastSave_ >= kLiveSaveSeconds;
    if (!dirty || (!force && !persistentDue && !liveDue)) {
        return;
    }
    persistentDirtyAt_.reset();
    liveDirty_ = false;
    lastSave_ = now;
    if (!loaded_.writable()) {
        if (!refusedSave_) {
            refusedSave_ = true;
            log("failed to save state: " + loaded_.problem);
        }
        return;
    }
    SavedState state;
    state.stations = registry_.records();
    state.automation = options_;
    state.bootId = loaded_.state.bootId;
    saveState(paths_.state, state, log);
}

void Service::waitForEvents(double seconds) {
    pollfd fds[5];
    nfds_t count = 0;
    for (const int fd : {bluez_.fd(), power_.fd(), widget_ ? widget_->wakeFd() : -1,
                         requestWatch_ ? requestWatch_->fd() : -1, stopSignalFd()}) {
        if (fd >= 0) {
            fds[count++] = pollfd{fd, POLLIN, 0};
        }
    }
    ::poll(fds, count, static_cast<int>(seconds * 1000.0));
}

void Service::createFolders() {
    for (const std::filesystem::path& folder : {paths_.runtimeDir, paths_.requests}) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            log("cannot create " + folder.string() + " (" + ec.message() + ")");
        }
    }
}

void Service::logStart() {
    log("base-station-control " + version_ + " running");
    const std::vector<const Station*> known = registry_.all();
    if (known.empty()) {
        log("waiting for Base Stations");
    } else {
        std::vector<std::string> names;
        for (const Station* station : known) {
            names.push_back(station->title() + " (" + station->name() + (station->record.ignored ? ", ignored" : "") + ")");
        }
        log("known base stations: " + joinWords(names, ", "));
    }
    if (!widget_) {
        log("Steam bar widget is turned off in config.json");
    }
    log("automatic switching: " + describeOptions(options_, apps_));
}

void Service::shutdown() {
    log("stopping");
    if (widget_) {
        widget_->stop();
        for (const std::string& message : widget_->take().logs) {
            log(message);
        }
    }
    const double now = monotonicSeconds();
    stopRun("service stopped", now);
    controller_.cancelAll();
    bluez_.shutdown();
    commands_.stop();
    saveIfDue(now, true);
    std::error_code ec;
    std::filesystem::remove(paths_.status, ec);
    log("base-station-control stopped");
}

int Service::run() {
    createFolders();
    requestWatch_ = std::make_unique<RequestWatch>(paths_.requests);
    logStart();
    const double start = monotonicSeconds();
    lastSave_ = start;
    applyOptions();
    checkBoot(start);

    while (!stopRequested()) {
        const double now = monotonicSeconds();
        power_.process(now);
        power_.tick(now);
        bluez_.process(now);
        bluez_.tick(now);
        controller_.tick(registry_, now);
        handleOutcomes(now);
        handlePowerEvents(now);
        run_.tick(registry_, controller_, now);
        finishRun(now);
        for (const AppEvent& event : appWatcher_.tick(now)) {
            handleAppEvent(event, now);
        }
        updateScanning(now);
        syncWidget(now);
        handleRequests(now);
        commands_.expire(now);
        writeStatus(now, false);
        saveIfDue(now, false);
        waitForEvents(busy() ? kBusyWaitSeconds : kIdleWaitSeconds);
    }
    shutdown();
    return 0;
}

}

LockResult acquireServiceLock(const std::filesystem::path& path, int* fd, std::string* error) {
    *fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (*fd < 0) {
        *error = "cannot create lock file " + path.string() + ": " + std::strerror(errno);
        return LockResult::Failed;
    }
    if (::flock(*fd, LOCK_EX | LOCK_NB) == 0) {
        return LockResult::Acquired;
    }
    const int code = errno;
    ::close(*fd);
    *fd = -1;
    if (code == EWOULDBLOCK) {
        *error = "base-station-control is already running (lock: " + path.string() + ")";
        return LockResult::AlreadyRunning;
    }
    *error = "cannot lock " + path.string() + ": " + std::strerror(code);
    return LockResult::Failed;
}

int runService(const Paths& paths, const std::string& version) {
    std::error_code ec;
    std::filesystem::create_directories(paths.runtimeDir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create %s: %s\n", paths.runtimeDir.string().c_str(), ec.message().c_str());
        return 1;
    }
    int lock = -1;
    std::string error;
    if (acquireServiceLock(paths.lock, &lock, &error) != LockResult::Acquired) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    int result = 0;
    {
        Service service(paths, version);
        result = service.run();
    }
    ::close(lock);
    return result;
}

}
