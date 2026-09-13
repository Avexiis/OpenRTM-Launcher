#include "gui.hpp"

#include "openrtm_icon_rgba.hpp"
#include "launcher_services.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace openrtm
{
namespace
{
const Fl_Color background = fl_rgb_color(38, 40, 43);
const Fl_Color headerBackground = fl_rgb_color(46, 48, 52);
const Fl_Color surface = fl_rgb_color(55, 58, 62);
const Fl_Color surfaceHover = fl_rgb_color(65, 68, 73);
const Fl_Color border = fl_rgb_color(75, 78, 84);
const Fl_Color text = fl_rgb_color(238, 239, 241);
const Fl_Color secondaryText = fl_rgb_color(176, 179, 184);
const Fl_Color mutedText = fl_rgb_color(137, 141, 147);
const Fl_Color green = fl_rgb_color(91, 190, 61);
const Fl_Color greenHover = fl_rgb_color(105, 207, 72);
const Fl_Color cyan = fl_rgb_color(41, 181, 207);
const Fl_Color amber = fl_rgb_color(225, 169, 55);
const Fl_Color red = fl_rgb_color(220, 83, 83);

enum class ButtonIcon
{
    play,
    refresh,
    download,
    install,
    trash
};

class StatusDot final : public Fl_Widget
{
public:
    StatusDot(const int x, const int y, const int width, const int height)
        : Fl_Widget(x, y, width, height)
    {
    }

    void statusColor(const Fl_Color color)
    {
        statusColor_ = color;
        redraw();
    }

    void draw() override
    {
        const int diameter = std::min(w(), h()) - 4;
        const int left = x() + (w() - diameter) / 2;
        const int top = y() + (h() - diameter) / 2;
        fl_color(fl_darker(statusColor_));
        fl_pie(left, top, diameter, diameter, 0, 360);
        fl_color(statusColor_);
        fl_pie(left + 2, top + 2, diameter - 4, diameter - 4, 0, 360);
    }

private:
    Fl_Color statusColor_ = mutedText;
};

class ActionButton final : public Fl_Button
{
public:
    ActionButton(const int x, const int y, const int width, const int height,
        const char* label, const ButtonIcon icon, const bool primary = false)
        : Fl_Button(x, y, width, height, label), icon_(icon), primary_(primary)
    {
        box(FL_NO_BOX);
        clear_visible_focus();
    }

    void buttonIcon(const ButtonIcon icon)
    {
        icon_ = icon;
        redraw();
    }

    int handle(const int event) override
    {
        if (event == FL_ENTER)
        {
            hovered_ = true;
            redraw();
            return 1;
        }
        if (event == FL_LEAVE)
        {
            hovered_ = false;
            redraw();
            return 1;
        }
        return Fl_Button::handle(event);
    }

    void draw() override
    {
        Fl_Color fill = primary_ ? green : surface;
        if (!active())
        {
            fill = fl_rgb_color(62, 64, 68);
        }
        else if (value())
        {
            fill = primary_ ? fl_darker(green) : fl_darker(surface);
        }
        else if (hovered_)
        {
            fill = primary_ ? greenHover : surfaceHover;
        }

        fl_color(fill);
        fl_rectf(x(), y(), w(), h());
        fl_color(primary_ ? fl_darker(green) : border);
        fl_rect(x(), y(), w(), h());

        const Fl_Color foreground = active()
            ? (primary_ ? fl_rgb_color(18, 27, 17) : text)
            : mutedText;
        fl_font(FL_HELVETICA_BOLD, 14);
        const int labelWidth = static_cast<int>(std::ceil(fl_width(label())));
        const int contentWidth = labelWidth + 27;
        const int iconCenterX = x() + std::max(12, (w() - contentWidth) / 2 + 7);
        const int iconCenterY = y() + h() / 2;
        drawIcon(iconCenterX, iconCenterY, foreground);

        fl_color(foreground);
        fl_draw(label(), iconCenterX + 14, y(), labelWidth + 2, h(),
            FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }

private:
    void drawIcon(const int centerX, const int centerY, const Fl_Color color) const
    {
        fl_color(color);
        fl_line_style(FL_SOLID, 2);
        switch (icon_)
        {
        case ButtonIcon::play:
            fl_begin_polygon();
            fl_vertex(centerX - 4, centerY - 7);
            fl_vertex(centerX + 7, centerY);
            fl_vertex(centerX - 4, centerY + 7);
            fl_end_polygon();
            break;
        case ButtonIcon::refresh:
            fl_arc(centerX - 7, centerY - 7, 14, 14, 35, 315);
            fl_begin_polygon();
            fl_vertex(centerX + 7, centerY - 5);
            fl_vertex(centerX + 2, centerY - 6);
            fl_vertex(centerX + 6, centerY - 1);
            fl_end_polygon();
            break;
        case ButtonIcon::download:
            fl_line(centerX, centerY - 7, centerX, centerY + 3);
            fl_line(centerX - 4, centerY, centerX, centerY + 4);
            fl_line(centerX, centerY + 4, centerX + 4, centerY);
            fl_line(centerX - 6, centerY + 7, centerX + 6, centerY + 7);
            break;
        case ButtonIcon::install:
            fl_line(centerX, centerY - 7, centerX, centerY + 2);
            fl_line(centerX - 4, centerY - 1, centerX, centerY + 3);
            fl_line(centerX, centerY + 3, centerX + 4, centerY - 1);
            fl_rect(centerX - 7, centerY + 6, 14, 3);
            break;
        case ButtonIcon::trash:
            fl_rect(centerX - 5, centerY - 4, 10, 11);
            fl_line(centerX - 7, centerY - 7, centerX + 7, centerY - 7);
            fl_line(centerX - 2, centerY - 9, centerX + 2, centerY - 9);
            break;
        }
        fl_line_style(0);
    }

    ButtonIcon icon_;
    bool primary_ = false;
    bool hovered_ = false;
};

std::string shortenedPath(const std::filesystem::path& path, const std::size_t maximum = 64)
{
    const std::string value = path.string();
    if (value.size() <= maximum)
    {
        return value;
    }
    const std::size_t side = (maximum - 3) / 2;
    return value.substr(0, side) + "..." + value.substr(value.size() - side);
}

std::string shortCommit(const std::string& commit)
{
    return commit.empty() ? std::string("Unknown version") : commit.substr(0, 12);
}

std::string jarSize(const std::uintmax_t bytes)
{
    if (bytes == 0)
    {
        return {};
    }
    const auto mebibytes = static_cast<unsigned long long>(bytes / (1024 * 1024));
    return " - " + std::to_string(mebibytes) + " MiB";
}

class LauncherUi final
{
public:
    LauncherUi()
        : iconSource_(openrtm_icon_rgba, 64, 64, 4), window_(700, 490, "OpenRTM Launcher")
    {
        window_.color(background);
        window_.size_range(700, 490, 700, 490);
        window_.callback(closeCallback, this);
        window_.begin();

        auto* header = new Fl_Box(0, 0, 700, 101);
        header->box(FL_FLAT_BOX);
        header->color(headerBackground);

        icon_.reset(iconSource_.copy(64, 64));
        auto* logo = new Fl_Box(28, 18, 64, 64);
        logo->image(icon_.get());
        window_.icon(icon_.get());

        auto* title = new Fl_Box(108, 25, 410, 31, "OpenRTM");
        styleLabel(title, 24, text, FL_HELVETICA_BOLD);
        auto* subtitle = new Fl_Box(108, 57, 410, 21, "Launcher and updater");
        styleLabel(subtitle, 13, secondaryText, FL_HELVETICA);

        version_ = new Fl_Box(570, 35, 98, 25,
            "v" OPENRTM_LAUNCHER_VERSION);
        styleLabel(version_, 12, mutedText, FL_HELVETICA);
        version_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

        auto* headerRule = new Fl_Box(0, 100, 700, 1);
        headerRule->box(FL_FLAT_BOX);
        headerRule->color(border);

        mainDot_ = new StatusDot(31, 126, 18, 18);
        statusTitle_ = new Fl_Box(59, 118, 609, 31, "Checking your system");
        styleLabel(statusTitle_, 20, text, FL_HELVETICA_BOLD);
        statusDetail_ = new Fl_Box(59, 151, 609, 48,
            "Looking for Java and the latest OpenRTM version.");
        styleLabel(statusDetail_, 13, secondaryText, FL_HELVETICA);
        statusDetail_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

        progress_ = new Fl_Progress(59, 209, 609, 8);
        progress_->minimum(0);
        progress_->maximum(100);
        progress_->value(0);
        progress_->box(FL_FLAT_BOX);
        progress_->color(surface);
        progress_->selection_color(cyan);
        progress_->hide();

        auto* systemLabel = new Fl_Box(32, 235, 636, 18, "SYSTEM");
        styleLabel(systemLabel, 11, mutedText, FL_HELVETICA_BOLD);

        javaDot_ = new StatusDot(31, 267, 18, 18);
        javaTitle_ = new Fl_Box(59, 258, 165, 24, "JDK");
        styleLabel(javaTitle_, 14, text, FL_HELVETICA_BOLD);
        javaDetail_ = new Fl_Box(224, 258, 444, 24, "Searching...");
        styleLabel(javaDetail_, 13, secondaryText, FL_HELVETICA);
        javaDetail_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

        auto* javaRule = new Fl_Box(59, 296, 609, 1);
        javaRule->box(FL_FLAT_BOX);
        javaRule->color(border);

        jarDot_ = new StatusDot(31, 316, 18, 18);
        jarTitle_ = new Fl_Box(59, 307, 165, 24, "OpenRTM");
        styleLabel(jarTitle_, 14, text, FL_HELVETICA_BOLD);
        jarDetail_ = new Fl_Box(224, 307, 444, 24, "Checking...");
        styleLabel(jarDetail_, 13, secondaryText, FL_HELVETICA);
        jarDetail_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

        auto* jarRule = new Fl_Box(59, 345, 609, 1);
        jarRule->box(FL_FLAT_BOX);
        jarRule->color(border);

        primary_ = new ActionButton(32, 370, 300, 48, "Launch OpenRTM",
            ButtonIcon::play, true);
        primary_->callback(primaryCallback, this);
        check_ = new ActionButton(344, 370, 210, 48, "Check for updates",
            ButtonIcon::refresh);
        check_->callback(checkCallback, this);
#ifdef _WIN32
        check_->resize(344, 370, 324, 48);
#else
        install_ = new ActionButton(566, 370, 102, 48, "Install", ButtonIcon::install);
        install_->callback(installCallback, this);
        install_->tooltip("Add OpenRTM to the desktop application menu");
#endif

        auto* footerRule = new Fl_Box(0, 441, 700, 1);
        footerRule->box(FL_FLAT_BOX);
        footerRule->color(border);
        dataPath_ = new Fl_Box(32, 451, 490, 26, "Data: ~/.openrtm");
        styleLabel(dataPath_, 11, mutedText, FL_HELVETICA);
        remove_ = new ActionButton(540, 449, 128, 30, "Remove", ButtonIcon::trash);
        remove_->callback(removeCallback, this);

        window_.end();
        setBusy(true);
    }

    ~LauncherUi()
    {
        cancelling_.store(true);
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    int run()
    {
        window_.show();
        beginCheck();
        return Fl::run();
    }

private:
    static void styleLabel(Fl_Box* label, const int size, const Fl_Color color,
        const Fl_Font font)
    {
        label->box(FL_NO_BOX);
        label->labelsize(size);
        label->labelfont(font);
        label->labelcolor(color);
        label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }

    static void closeCallback(Fl_Widget*, void* context)
    {
        auto& self = *static_cast<LauncherUi*>(context);
        self.cancelling_.store(true);
        self.window_.hide();
    }

    static void primaryCallback(Fl_Widget*, void* context)
    {
        static_cast<LauncherUi*>(context)->primaryAction();
    }

    static void checkCallback(Fl_Widget*, void* context)
    {
        static_cast<LauncherUi*>(context)->beginCheck();
    }

    static void installCallback(Fl_Widget*, void* context)
    {
        static_cast<LauncherUi*>(context)->beginInstall();
    }

    static void removeCallback(Fl_Widget*, void* context)
    {
        static_cast<LauncherUi*>(context)->beginUninstall();
    }

    static void awakeCallback(void* context)
    {
        static_cast<LauncherUi*>(context)->drainUiQueue();
    }

    void post(std::function<void()> callback)
    {
        {
            const std::scoped_lock lock(queueMutex_);
            uiQueue_.push_back(std::move(callback));
        }
        Fl::awake(awakeCallback, this);
    }

    void drainUiQueue()
    {
        std::vector<std::function<void()>> callbacks;
        {
            const std::scoped_lock lock(queueMutex_);
            callbacks.swap(uiQueue_);
        }
        for (auto& callback : callbacks)
        {
            callback();
        }
    }

    void startWorker(std::function<void()> operation)
    {
        if (busy_)
        {
            return;
        }
        if (worker_.joinable())
        {
            worker_.join();
        }
        cancelling_.store(false);
        setBusy(true);
        worker_ = std::thread([this, operation = std::move(operation)]() {
            try
            {
                operation();
            }
            catch (const std::exception& error)
            {
                if (!cancelling_.load())
                {
                    const std::string message = error.what();
                    post([this, message]() { showFailure(message); });
                }
            }
        });
    }

    void beginCheck()
    {
        setStatus("Checking your system", "Looking for Java and the latest OpenRTM version.",
            mutedText);
        progress_->hide();
        busy_ = false;
        startWorker([this]() {
            LauncherSnapshot snapshot = inspectLauncherStatus();
            if (!cancelling_.load())
            {
                post([this, snapshot = std::move(snapshot)]() mutable {
                    applySnapshot(std::move(snapshot));
                });
            }
        });
    }

    void primaryAction()
    {
        if (!snapshot_.javaAvailable)
        {
            try
            {
                openExternalUrl(jdkDownloadUrlForGui());
            }
            catch (const std::exception& error)
            {
                showFailure(error.what());
            }
            return;
        }

        setStatus(snapshot_.jarAvailable ? "Preparing OpenRTM" : "Installing OpenRTM",
            "Checking for the newest available version.", cyan);
        progress_->value(0);
        progress_->show();
        busy_ = false;
        startWorker([this]() {
            int lastPercentage = -1;
            std::uint64_t lastBytes = 0;
            prepareLatestForGui(false,
                [this](const std::string& message) {
                    if (!cancelling_.load())
                    {
                        post([this, message]() { setStatus(message, statusDetailText_, cyan); });
                    }
                },
                [this, &lastPercentage, &lastBytes](const std::uint64_t current,
                    const std::optional<std::uint64_t> total) {
                    if (cancelling_.load())
                    {
                        throw std::runtime_error("Download canceled");
                    }
                    int percentage = -1;
                    if (total && *total > 0)
                    {
                        percentage = static_cast<int>(std::min<std::uint64_t>(100,
                            current * 100 / *total));
                        if (percentage == lastPercentage)
                        {
                            return;
                        }
                        lastPercentage = percentage;
                    }
                    else
                    {
                        if (current < lastBytes + 1024 * 1024)
                        {
                            return;
                        }
                        lastBytes = current;
                    }
                    post([this, current, total, percentage]() {
                        updateProgress(current, total, percentage);
                    });
                });
            if (cancelling_.load())
            {
                return;
            }
            launchForGui();
            post([this]() {
                setBusy(false);
                setStatus("OpenRTM started", "The launcher is closing.", green);
                progress_->hide();
                window_.hide();
            });
        });
    }

    void beginInstall()
    {
#ifndef _WIN32
        setStatus("Installing desktop entry", "Adding OpenRTM to the application menu.", cyan);
        busy_ = false;
        startWorker([this]() {
            installDesktopForGui();
            post([this]() {
                snapshot_.desktopInstalled = true;
                install_->copy_label("Installed");
                install_->deactivate();
                setBusy(false);
                setStatus("Desktop entry installed", "OpenRTM is available in the application menu.",
                    green);
            });
        });
#endif
    }

    void beginUninstall()
    {
        const int choice = fl_choice(
            "Remove OpenRTM, its downloaded JAR, and all settings?\n\nThis cannot be undone.",
            "Cancel", "Remove", nullptr);
        if (choice != 1)
        {
            return;
        }

        setStatus("Removing OpenRTM", "Deleting application data and desktop integration.", amber);
        busy_ = false;
        startWorker([this]() {
            uninstallForGui();
            post([this]() {
                snapshot_.jarAvailable = false;
                snapshot_.jarBytes = 0;
                snapshot_.localCommit.clear();
                snapshot_.desktopInstalled = false;
                applySnapshot(snapshot_);
                setStatus("OpenRTM was removed", "Application data and settings were deleted.", green);
            });
        });
    }

    void applySnapshot(LauncherSnapshot snapshot)
    {
        snapshot_ = std::move(snapshot);
        setBusy(false);
        progress_->hide();

        dataPathText_ = "Data: " + shortenedPath(snapshot_.dataDirectory, 58);
        dataPath_->copy_label(dataPathText_.c_str());
        dataPath_->copy_tooltip(snapshot_.dataDirectory.string().c_str());

        if (snapshot_.javaAvailable)
        {
            javaDot_->statusColor(green);
            javaTitle_->copy_label(("JDK " + std::to_string(snapshot_.javaMajor)).c_str());
            javaDetailText_ = shortenedPath(snapshot_.javaHome);
            javaDetail_->copy_label(javaDetailText_.c_str());
            javaDetail_->copy_tooltip(snapshot_.javaHome.string().c_str());
        }
        else
        {
            javaDot_->statusColor(red);
            javaTitle_->copy_label("JDK 17 required");
            javaDetail_->copy_label("Not found");
            javaDetail_->tooltip(nullptr);
        }

        if (snapshot_.jarAvailable)
        {
            jarDot_->statusColor(green);
            jarDetailText_ = shortCommit(snapshot_.localCommit) + jarSize(snapshot_.jarBytes);
            jarDetail_->copy_label(jarDetailText_.c_str());
        }
        else
        {
            jarDot_->statusColor(snapshot_.remoteCommit.empty() ? red : amber);
            jarDetail_->copy_label("Not installed");
        }

#ifndef _WIN32
        if (snapshot_.desktopInstalled)
        {
            install_->copy_label("Installed");
            install_->deactivate();
        }
        else
        {
            install_->copy_label("Install");
            install_->activate();
        }
#endif

        if (!snapshot_.javaAvailable)
        {
            primary_->copy_label("Get OpenJDK 17");
            primary_->buttonIcon(ButtonIcon::download);
            primary_->activate();
            setStatus("JDK 17 is required",
                "Install OpenJDK 17, then check your system again.", red);
            return;
        }

        primary_->buttonIcon(ButtonIcon::play);
        primary_->activate();
        if (!snapshot_.updateError.empty())
        {
            if (snapshot_.jarAvailable)
            {
                primary_->copy_label("Launch OpenRTM");
                setStatus("OpenRTM is ready offline",
                    "The update server could not be reached. The installed version can still run.",
                    amber);
            }
            else
            {
                primary_->copy_label("Try again");
                setStatus("OpenRTM could not be downloaded",
                    "Check your internet connection and try again.", red);
            }
            return;
        }

        const bool updateAvailable = !snapshot_.jarAvailable
            || snapshot_.localCommit != snapshot_.remoteCommit;
        if (updateAvailable)
        {
            primary_->copy_label(snapshot_.jarAvailable ? "Update & Launch" : "Install & Launch");
            setStatus(snapshot_.jarAvailable ? "An update is available" : "OpenRTM is ready to install",
                "Latest version " + shortCommit(snapshot_.remoteCommit), cyan);
        }
        else
        {
            primary_->copy_label("Launch OpenRTM");
            setStatus("OpenRTM is ready", "Version " + shortCommit(snapshot_.localCommit), green);
        }
    }

    void updateProgress(const std::uint64_t current,
        const std::optional<std::uint64_t> total, const int percentage)
    {
        progress_->show();
        std::string detail;
        if (percentage >= 0 && total)
        {
            progress_->value(percentage);
            detail = "Downloading OpenRTM.jar - " + std::to_string(percentage) + "% ("
                + std::to_string(current / (1024 * 1024)) + " of "
                + std::to_string(*total / (1024 * 1024)) + " MiB)";
        }
        else
        {
            progress_->value(0);
            detail = "Downloading OpenRTM.jar - "
                + std::to_string(current / (1024 * 1024)) + " MiB";
        }
        setStatus("Downloading the latest OpenRTM update", detail, cyan);
    }

    void showFailure(const std::string& message)
    {
        setBusy(false);
        progress_->hide();
        setStatus("Something went wrong", message, red);
    }

    void setBusy(const bool busy)
    {
        busy_ = busy;
        if (busy)
        {
            primary_->deactivate();
            check_->deactivate();
            remove_->deactivate();
#ifndef _WIN32
            install_->deactivate();
#endif
        }
        else
        {
            primary_->activate();
            check_->activate();
            remove_->activate();
#ifndef _WIN32
            if (!snapshot_.desktopInstalled)
            {
                install_->activate();
            }
#endif
        }
    }

    void setStatus(const std::string& titleValue, const std::string& detailValue,
        const Fl_Color color)
    {
        statusTitleText_ = titleValue;
        statusDetailText_ = detailValue;
        statusTitle_->copy_label(statusTitleText_.c_str());
        statusDetail_->copy_label(statusDetailText_.c_str());
        mainDot_->statusColor(color);
    }

    Fl_RGB_Image iconSource_;
    std::unique_ptr<Fl_Image> icon_;
    Fl_Window window_;
    Fl_Box* version_ = nullptr;
    StatusDot* mainDot_ = nullptr;
    Fl_Box* statusTitle_ = nullptr;
    Fl_Box* statusDetail_ = nullptr;
    Fl_Progress* progress_ = nullptr;
    StatusDot* javaDot_ = nullptr;
    Fl_Box* javaTitle_ = nullptr;
    Fl_Box* javaDetail_ = nullptr;
    StatusDot* jarDot_ = nullptr;
    Fl_Box* jarTitle_ = nullptr;
    Fl_Box* jarDetail_ = nullptr;
    ActionButton* primary_ = nullptr;
    ActionButton* check_ = nullptr;
    ActionButton* install_ = nullptr;
    ActionButton* remove_ = nullptr;
    Fl_Box* dataPath_ = nullptr;

    LauncherSnapshot snapshot_;
    std::thread worker_;
    std::atomic_bool cancelling_ = false;
    bool busy_ = false;
    std::mutex queueMutex_;
    std::vector<std::function<void()>> uiQueue_;
    std::string statusTitleText_;
    std::string statusDetailText_;
    std::string javaDetailText_;
    std::string jarDetailText_;
    std::string dataPathText_;
};
}

int runGraphicalLauncher()
{
#ifndef _WIN32
    Fl::set_fonts(nullptr);
#endif
    Fl::scheme("gtk+");
    Fl::set_color(FL_BACKGROUND_COLOR, background);
    Fl::set_color(FL_BACKGROUND2_COLOR, surface);
    Fl::set_color(FL_FOREGROUND_COLOR, text);
    Fl::set_color(FL_SELECTION_COLOR, cyan);
    Fl::lock();

    LauncherUi application;
    return application.run();
}
}
