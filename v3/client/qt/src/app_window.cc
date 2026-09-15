#include "chat_client/app_window.hpp"

#include "file.pb.h"
#include "gateway.pb.h"
#include "message.pb.h"
#include "notify.pb.h"
#include "transmit.pb.h"
#include "user.pb.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QSizeGrip>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>

namespace chat::desktop {
namespace {

constexpr int kEntityIdRole = Qt::UserRole;
constexpr int kEntityAuxRole = Qt::UserRole + 1;
constexpr int kSerializedMessageRole = Qt::UserRole + 2;
constexpr int kEntityFilterRole = Qt::UserRole + 3;
constexpr int kDialogTitleBarHeight = 42;
constexpr int kDialogBorderWidth = 3;

class DialogTitleBar final : public QFrame {
public:
    explicit DialogTitleBar(QDialog* dialog)
        : QFrame(dialog), dialog_(dialog) {
        setObjectName(QStringLiteral("dialogTitleBar"));
        setAttribute(Qt::WA_StyledBackground, true);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(14, 0, 0, 0);
        layout->setSpacing(0);
        title_ = new QLabel(dialog->windowTitle());
        title_->setObjectName(QStringLiteral("dialogWindowTitle"));
        title_->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* close = new QPushButton(QStringLiteral("×"));
        close->setObjectName(QStringLiteral("dialogCloseButton"));
        close->setFixedSize(46, kDialogTitleBarHeight);
        close->setToolTip(QStringLiteral("关闭"));
        layout->addWidget(title_);
        layout->addStretch();
        layout->addWidget(close);
        connect(close, &QPushButton::clicked, dialog_, &QDialog::reject);
    }

    void set_title(const QString& title) {
        title_->setText(title);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            dragging_ = true;
            drag_offset_ = event->globalPos() - dialog_->frameGeometry().topLeft();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ && (event->buttons() & Qt::LeftButton)) {
            dialog_->move(event->globalPos() - drag_offset_);
            event->accept();
            return;
        }
        QFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        dragging_ = false;
        QFrame::mouseReleaseEvent(event);
    }

private:
    QDialog* dialog_;
    QLabel* title_ = nullptr;
    QPoint drag_offset_;
    bool dragging_ = false;
};

class DialogChromeManager final : public QObject {
public:
    explicit DialogChromeManager(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (!dialog) {
            return QObject::eventFilter(watched, event);
        }
        if (event->type() == QEvent::Polish || event->type() == QEvent::Show) {
            decorate(dialog);
            sync(dialog);
        } else if (event->type() == QEvent::Resize) {
            sync(dialog);
        } else if (event->type() == QEvent::WindowTitleChange) {
            if (auto* bar = title_bar(dialog)) {
                bar->set_title(dialog->windowTitle());
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static DialogTitleBar* title_bar(QDialog* dialog) {
        return dialog->findChild<DialogTitleBar*>(
            QStringLiteral("dialogTitleBar"), Qt::FindDirectChildrenOnly);
    }

    static void decorate(QDialog* dialog) {
        if (!dialog->property("chatDialogDecorated").toBool()) {
            dialog->setProperty("chatDialogDecorated", true);
            dialog->setWindowFlag(Qt::FramelessWindowHint, true);
            dialog->setAttribute(Qt::WA_StyledBackground, true);
            auto* bar = new DialogTitleBar(dialog);
            bar->show();
            auto* grip = new QSizeGrip(dialog);
            grip->setObjectName(QStringLiteral("dialogSizeGrip"));
            grip->setFixedSize(18, 18);
            grip->show();
        }
        if (!dialog->property("chatDialogMarginApplied").toBool() && dialog->layout()) {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
            dialog->layout()->getContentsMargins(&left, &top, &right, &bottom);
            dialog->layout()->setContentsMargins(
                std::max(left, 12),
                top + kDialogTitleBarHeight + kDialogBorderWidth,
                std::max(right, 12),
                std::max(bottom, 12));
            dialog->setProperty("chatDialogMarginApplied", true);
        }
    }

    static void sync(QDialog* dialog) {
        if (auto* bar = title_bar(dialog)) {
            bar->setGeometry(
                kDialogBorderWidth,
                kDialogBorderWidth,
                std::max(0, dialog->width() - kDialogBorderWidth * 2),
                kDialogTitleBarHeight);
            bar->raise();
        }
        if (auto* grip = dialog->findChild<QSizeGrip*>(
                QStringLiteral("dialogSizeGrip"), Qt::FindDirectChildrenOnly)) {
            grip->move(dialog->width() - grip->width() - kDialogBorderWidth,
                       dialog->height() - grip->height() - kDialogBorderWidth);
            grip->raise();
        }
    }
};

QString text(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QByteArray bytes(const std::string& value) {
    return QByteArray(value.data(), static_cast<int>(value.size()));
}

std::string utf8(const QString& value) {
    const QByteArray encoded = value.toUtf8();
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

template <typename Response>
QString response_error(const Response& response, const QString& transport_error) {
    if (!transport_error.isEmpty()) {
        return transport_error;
    }
    if (!response.success()) {
        return response.errmsg().empty()
            ? QStringLiteral("服务器拒绝了请求")
            : text(response.errmsg());
    }
    return {};
}

QString file_size_text(std::int64_t size) {
    if (size >= 1024LL * 1024LL) {
        return QStringLiteral("%1 MiB").arg(
            static_cast<double>(size) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (size >= 1024LL) {
        return QStringLiteral("%1 KiB").arg(
            static_cast<double>(size) / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(size);
}

QLabel* make_icon_label(const QIcon& icon, int edge, const QString& object_name) {
    auto* label = new QLabel;
    label->setObjectName(object_name);
    label->setFixedSize(edge, edge);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(icon.pixmap(edge, edge));
    return label;
}

QIcon session_avatar_icon(const ahwei_im::ChatSessionInfo& session) {
    QPixmap avatar;
    if (!session.avatar().empty()) {
        avatar.loadFromData(bytes(session.avatar()));
    }
    if (!avatar.isNull()) {
        return QIcon(avatar.scaled(
            80, 80, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    return session.single_chat_friend_id().empty()
        ? QIcon(QStringLiteral(":/chat/group-avatar.png"))
        : QIcon(QStringLiteral(":/chat/default-avatar.png"));
}

void install_entity_row(
    QListWidget* list,
    QListWidgetItem* item,
    const QIcon& icon,
    const QString& title,
    const QString& subtitle,
    int unread = 0) {
    item->setData(kEntityFilterRole, title + QStringLiteral("\n") + subtitle);
    item->setSizeHint(QSize(0, 76));

    auto* row = new QWidget;
    row->setObjectName(QStringLiteral("entityRow"));
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 12, 10);
    layout->setSpacing(12);
    layout->addWidget(make_icon_label(icon, 46, QStringLiteral("entityAvatar")));

    auto* text_layout = new QVBoxLayout;
    text_layout->setContentsMargins(0, 1, 0, 1);
    text_layout->setSpacing(4);
    auto* title_row = new QHBoxLayout;
    title_row->setContentsMargins(0, 0, 0, 0);
    title_row->setSpacing(8);
    auto* title_label = new QLabel(title);
    title_label->setObjectName(QStringLiteral("entityTitle"));
    title_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title_row->addWidget(title_label, 1);
    if (unread > 0) {
        auto* badge = new QLabel(unread > 99 ? QStringLiteral("99+") : QString::number(unread));
        badge->setObjectName(QStringLiteral("unreadBadge"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setMinimumWidth(20);
        badge->setFixedHeight(20);
        title_row->addWidget(badge);
    }
    auto* subtitle_label = new QLabel(subtitle);
    subtitle_label->setObjectName(QStringLiteral("entitySubtitle"));
    subtitle_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    text_layout->addLayout(title_row);
    text_layout->addWidget(subtitle_label);
    layout->addLayout(text_layout, 1);

    list->setItemWidget(item, row);
}

}  // namespace

AppWindow::AppWindow(
    const QUrl& initial_http_url,
    const QUrl& initial_websocket_url,
    QWidget* parent)
    : QWidget(parent) {
    if (!qApp->property("chatDialogChromeInstalled").toBool()) {
        qApp->setProperty("chatDialogChromeInstalled", true);
        qApp->installEventFilter(new DialogChromeManager(qApp));
    }
    setWindowTitle(QStringLiteral("聊天"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    resize(1220, 780);
    setMinimumSize(1000, 680);

    pages_ = new QStackedWidget(this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(build_window_title_bar());
    root->addWidget(pages_, 1);
    build_login_page();
    build_chat_page();
    apply_style();
    wire_network_events();

    http_url_edit_->setText(initial_http_url.toString());
    websocket_url_edit_->setText(initial_websocket_url.toString());
    pages_->setCurrentWidget(login_page_);
}

void AppWindow::closeEvent(QCloseEvent* event) {
    intentional_logout_ = true;
    gateway_.close_websocket();
    QWidget::closeEvent(event);
}

bool AppWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched != window_title_bar_) {
        return QWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            isMaximized() ? showNormal() : showMaximized();
            window_dragging_ = false;
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !isMaximized()) {
            window_dragging_ = true;
            window_drag_offset_ = mouse->globalPos() - frameGeometry().topLeft();
            return true;
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (window_dragging_ && (mouse->buttons() & Qt::LeftButton) && !isMaximized()) {
            move(mouse->globalPos() - window_drag_offset_);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        window_dragging_ = false;
    }
    return QWidget::eventFilter(watched, event);
}

QWidget* AppWindow::build_window_title_bar() {
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("windowTitleBar"));
    bar->setFixedHeight(42);
    bar->installEventFilter(this);
    window_title_bar_ = bar;

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 0, 0, 0);
    layout->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("聊天"));
    title->setObjectName(QStringLiteral("windowTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* minimize = new QPushButton(QStringLiteral("—"));
    auto* maximize = new QPushButton(QStringLiteral("□"));
    auto* close = new QPushButton(QStringLiteral("×"));
    minimize->setObjectName(QStringLiteral("windowControlButton"));
    maximize->setObjectName(QStringLiteral("windowControlButton"));
    close->setObjectName(QStringLiteral("windowCloseButton"));
    for (auto* button : {minimize, maximize, close}) {
        button->setFixedSize(48, 42);
    }
    layout->addWidget(title);
    layout->addStretch();
    layout->addWidget(minimize);
    layout->addWidget(maximize);
    layout->addWidget(close);

    connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(maximize, &QPushButton::clicked, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    return bar;
}

void AppWindow::build_login_page() {
    login_page_ = new QWidget;
    pages_->addWidget(login_page_);
    auto* outer = new QVBoxLayout(login_page_);
    outer->setContentsMargins(30, 30, 30, 30);
    outer->addStretch();

    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("loginCard"));
    card->setMaximumWidth(520);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(44, 34, 44, 34);
    card_layout->setSpacing(16);

    auto* brand = new QLabel(QStringLiteral("聊天"));
    brand->setObjectName(QStringLiteral("brandTitle"));
    brand->setAlignment(Qt::AlignCenter);
    auto* subtitle = new QLabel(QStringLiteral("和朋友保持联系"));
    subtitle->setObjectName(QStringLiteral("subtleLabel"));
    subtitle->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(brand);
    card_layout->addWidget(subtitle);

    auto* tabs = new QTabWidget;
    tabs->addTab(build_account_login_tab(), QStringLiteral("账号登录"));
    tabs->addTab(build_phone_login_tab(), QStringLiteral("手机登录"));
    card_layout->addWidget(tabs);

    http_url_edit_ = new QLineEdit(login_page_);
    websocket_url_edit_ = new QLineEdit(login_page_);
    http_url_edit_->hide();
    websocket_url_edit_->hide();

    login_status_ = new QLabel(QStringLiteral("请输入账号，或先注册新用户"));
    login_status_->setObjectName(QStringLiteral("subtleLabel"));
    login_status_->setWordWrap(true);
    login_status_->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(login_status_);

    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(card);
    row->addStretch();
    outer->addLayout(row);
    outer->addStretch();
}

QWidget* AppWindow::build_account_login_tab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 20, 12, 16);
    layout->setSpacing(12);
    username_edit_ = new QLineEdit;
    password_edit_ = new QLineEdit;
    username_edit_->setPlaceholderText(QStringLiteral("请输入用户名"));
    password_edit_->setPlaceholderText(QStringLiteral("请输入密码"));
    password_edit_->setEchoMode(QLineEdit::Password);
    account_login_button_ = new QPushButton(QStringLiteral("登录"));
    account_login_button_->setObjectName(QStringLiteral("primaryButton"));
    account_register_button_ = new QPushButton(QStringLiteral("注册"));
    layout->addWidget(username_edit_);
    layout->addWidget(password_edit_);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(account_login_button_);
    buttons->addWidget(account_register_button_);
    layout->addLayout(buttons);
    connect(account_login_button_, &QPushButton::clicked, this, &AppWindow::account_login);
    connect(account_register_button_, &QPushButton::clicked, this, &AppWindow::account_register);
    connect(password_edit_, &QLineEdit::returnPressed, this, &AppWindow::account_login);
    return page;
}

QWidget* AppWindow::build_phone_login_tab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 20, 12, 16);
    layout->setSpacing(12);
    phone_edit_ = new QLineEdit;
    phone_code_edit_ = new QLineEdit;
    phone_edit_->setPlaceholderText(QStringLiteral("11 位手机号"));
    phone_code_edit_->setPlaceholderText(QStringLiteral("请输入验证码"));
    phone_code_button_ = new QPushButton(QStringLiteral("获取验证码"));
    phone_login_button_ = new QPushButton(QStringLiteral("手机登录"));
    phone_login_button_->setObjectName(QStringLiteral("primaryButton"));
    phone_register_button_ = new QPushButton(QStringLiteral("手机注册"));
    layout->addWidget(phone_edit_);
    auto* code_row = new QHBoxLayout;
    code_row->addWidget(phone_code_edit_, 1);
    code_row->addWidget(phone_code_button_);
    layout->addLayout(code_row);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(phone_login_button_);
    buttons->addWidget(phone_register_button_);
    layout->addLayout(buttons);
    connect(phone_code_button_, &QPushButton::clicked, this, &AppWindow::request_phone_code);
    connect(phone_login_button_, &QPushButton::clicked, this, [this] { phone_login(false); });
    connect(phone_register_button_, &QPushButton::clicked, this, [this] { phone_login(true); });
    return page;
}

void AppWindow::build_chat_page() {
    chat_page_ = new QWidget;
    pages_->addWidget(chat_page_);
    auto* main_layout = new QHBoxLayout(chat_page_);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    auto* navigation = new QFrame;
    navigation->setObjectName(QStringLiteral("navigation"));
    navigation->setFixedWidth(76);
    auto* navigation_layout = new QVBoxLayout(navigation);
    navigation_layout->setContentsMargins(10, 22, 10, 16);
    navigation_layout->setSpacing(8);
    profile_button_ = new QPushButton;
    profile_button_->setObjectName(QStringLiteral("avatarButton"));
    profile_button_->setIcon(QIcon(QStringLiteral(":/chat/default-avatar.png")));
    profile_button_->setIconSize(QSize(44, 44));
    profile_button_->setFixedSize(52, 52);
    sessions_button_ = new QPushButton(QStringLiteral("消息"));
    friends_button_ = new QPushButton(QStringLiteral("好友"));
    applications_button_ = new QPushButton(QStringLiteral("申请"));
    for (auto* button : {sessions_button_, friends_button_, applications_button_}) {
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("navButton"));
        button->setFixedHeight(54);
    }
    connection_label_ = new QLabel(QStringLiteral("离线"));
    connection_label_->setObjectName(QStringLiteral("connectionLabel"));
    connection_label_->setAlignment(Qt::AlignCenter);
    connection_label_->setWordWrap(true);
    auto* logout_button = new QPushButton(QStringLiteral("退出"));
    logout_button->setObjectName(QStringLiteral("navButton"));
    navigation_layout->addWidget(profile_button_, 0, Qt::AlignHCenter);
    navigation_layout->addSpacing(12);
    navigation_layout->addWidget(sessions_button_);
    navigation_layout->addWidget(friends_button_);
    navigation_layout->addWidget(applications_button_);
    navigation_layout->addStretch();
    navigation_layout->addWidget(connection_label_);
    navigation_layout->addWidget(logout_button);

    auto* entities = new QFrame;
    entities->setObjectName(QStringLiteral("entityPanel"));
    entities->setMinimumWidth(310);
    entities->setMaximumWidth(410);
    auto* entity_layout = new QVBoxLayout(entities);
    entity_layout->setContentsMargins(0, 0, 0, 0);
    entity_layout->setSpacing(0);
    auto* entity_header = new QHBoxLayout;
    entity_header->setContentsMargins(14, 14, 12, 12);
    entity_header->setSpacing(8);
    filter_edit_ = new QLineEdit;
    filter_edit_->setObjectName(QStringLiteral("listSearch"));
    filter_edit_->setPlaceholderText(QStringLiteral("筛选当前列表"));
    list_action_button_ = new QPushButton(QStringLiteral("＋"));
    list_action_button_->setObjectName(QStringLiteral("listToolButton"));
    list_action_button_->setToolTip(QStringLiteral("添加好友或创建群聊"));
    refresh_button_ = new QPushButton(QStringLiteral("↻"));
    refresh_button_->setObjectName(QStringLiteral("listToolButton"));
    refresh_button_->setToolTip(QStringLiteral("刷新"));
    list_action_button_->setFixedSize(40, 40);
    refresh_button_->setFixedSize(40, 40);
    entity_header->addWidget(filter_edit_, 1);
    entity_header->addWidget(list_action_button_);
    entity_header->addWidget(refresh_button_);
    entity_list_ = new QListWidget;
    entity_list_->setObjectName(QStringLiteral("entityList"));
    entity_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    entity_list_->setIconSize(QSize(46, 46));
    entity_list_->setSpacing(0);
    entity_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    entity_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    entity_layout->addLayout(entity_header);
    entity_layout->addWidget(entity_list_, 1);

    auto* conversation = new QFrame;
    conversation->setObjectName(QStringLiteral("conversationPanel"));
    auto* conversation_layout = new QVBoxLayout(conversation);
    conversation_layout->setContentsMargins(0, 0, 0, 0);
    conversation_layout->setSpacing(0);
    auto* title_bar = new QFrame;
    title_bar->setObjectName(QStringLiteral("titleBar"));
    title_bar->setFixedHeight(66);
    auto* title_layout = new QHBoxLayout(title_bar);
    title_layout->setContentsMargins(26, 0, 22, 0);
    title_layout->setSpacing(8);
    conversation_title_ = new QLabel(QStringLiteral("请选择一个会话"));
    conversation_title_->setObjectName(QStringLiteral("conversationTitle"));
    members_button_ = new QPushButton(QStringLiteral("成员"));
    history_button_ = new QPushButton(QStringLiteral("历史"));
    members_button_->setObjectName(QStringLiteral("headerButton"));
    history_button_->setObjectName(QStringLiteral("headerButton"));
    members_button_->setEnabled(false);
    history_button_->setEnabled(false);
    title_layout->addWidget(conversation_title_, 1);
    title_layout->addWidget(members_button_);
    title_layout->addWidget(history_button_);
    message_list_ = new QListWidget;
    message_list_->setObjectName(QStringLiteral("messageList"));
    message_list_->setSpacing(0);
    message_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    message_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    message_list_->setUniformItemSizes(false);
    auto* composer_panel = new QFrame;
    composer_panel->setObjectName(QStringLiteral("composerPanel"));
    composer_panel->setFixedHeight(210);
    auto* composer_layout = new QVBoxLayout(composer_panel);
    composer_layout->setContentsMargins(22, 10, 22, 14);
    composer_layout->setSpacing(6);
    auto* tools = new QHBoxLayout;
    tools->setSpacing(2);
    image_button_ = new QPushButton(QStringLiteral("图片"));
    file_button_ = new QPushButton(QStringLiteral("文件"));
    speech_button_ = new QPushButton(QStringLiteral("语音"));
    for (auto* button : {image_button_, file_button_, speech_button_}) {
        button->setObjectName(QStringLiteral("composerToolButton"));
        button->setFixedHeight(34);
    }
    speech_button_->setEnabled(false);
    speech_button_->setToolTip(QStringLiteral("语音功能暂不可用"));
    tools->addWidget(image_button_);
    tools->addWidget(file_button_);
    tools->addWidget(speech_button_);
    tools->addStretch();
    composer_ = new QPlainTextEdit;
    composer_->setObjectName(QStringLiteral("messageComposer"));
    composer_->setPlaceholderText(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    send_button_ = new QPushButton(QStringLiteral("发送"));
    send_button_->setObjectName(QStringLiteral("primaryButton"));
    send_button_->setFixedSize(100, 38);
    auto* send_row = new QHBoxLayout;
    send_row->addStretch();
    send_row->addWidget(send_button_);
    composer_layout->addLayout(tools);
    composer_layout->addWidget(composer_, 1);
    composer_layout->addLayout(send_row);
    status_label_ = new QLabel(QStringLiteral("就绪"));
    status_label_->setObjectName(QStringLiteral("statusBar"));
    status_label_->setFixedHeight(26);
    conversation_layout->addWidget(title_bar);
    conversation_layout->addWidget(message_list_, 1);
    conversation_layout->addWidget(composer_panel);
    conversation_layout->addWidget(status_label_);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(entities);
    splitter->addWidget(conversation);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setHandleWidth(1);
    splitter->setSizes(QList<int>{330, 810});
    main_layout->addWidget(navigation);
    main_layout->addWidget(splitter, 1);

    connect(profile_button_, &QPushButton::clicked, this, &AppWindow::show_profile_dialog);
    connect(sessions_button_, &QPushButton::clicked, this, [this] { switch_list_mode(ListMode::SESSIONS); });
    connect(friends_button_, &QPushButton::clicked, this, [this] { switch_list_mode(ListMode::FRIENDS); });
    connect(applications_button_, &QPushButton::clicked, this, [this] { switch_list_mode(ListMode::APPLICATIONS); });
    connect(logout_button, &QPushButton::clicked, this, [this] { logout(); });
    connect(refresh_button_, &QPushButton::clicked, this, &AppWindow::refresh_visible_list);
    connect(filter_edit_, &QLineEdit::textChanged, this, &AppWindow::filter_visible_list);
    connect(list_action_button_, &QPushButton::clicked, this, [this] {
        if (list_mode_ == ListMode::SESSIONS) {
            show_create_group_dialog();
        } else if (list_mode_ == ListMode::FRIENDS) {
            show_add_friend_dialog();
        } else {
            refresh_applications();
        }
    });
    connect(entity_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { handle_list_item(item); });
    connect(entity_list_, &QListWidget::customContextMenuRequested, this, &AppWindow::show_list_context_menu);
    connect(send_button_, &QPushButton::clicked, this, &AppWindow::send_text_message);
    auto* send_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), composer_);
    connect(send_shortcut, &QShortcut::activated, this, &AppWindow::send_text_message);
    connect(image_button_, &QPushButton::clicked, this, &AppWindow::send_image_message);
    connect(file_button_, &QPushButton::clicked, this, &AppWindow::send_file_message);
    connect(members_button_, &QPushButton::clicked, this, &AppWindow::show_session_members);
    connect(history_button_, &QPushButton::clicked, this, &AppWindow::show_history_menu);
    connect(message_list_, &QListWidget::itemDoubleClicked, this, &AppWindow::open_message_attachment);
}

void AppWindow::apply_style() {
    qApp->setStyleSheet(QStringLiteral(R"(
        QWidget {
            font-family: "Noto Sans CJK SC", "Microsoft YaHei", sans-serif;
            color: #222222;
        }
        #windowTitleBar { background: #07c160; border: none; }
        #windowTitle { color: white; font-size: 15px; font-weight: 600; background: transparent; }
        #windowControlButton, #windowCloseButton {
            color: white;
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 0;
            font-size: 16px;
        }
        #windowControlButton:hover { background: #06ad56; }
        #windowCloseButton:hover { background: #e34d59; }
        QDialog { background: #f7f7f7; border: 3px solid #07c160; }
        #dialogTitleBar { background: #07c160; border: none; }
        #dialogWindowTitle { color: white; font-size: 14px; font-weight: 600; background: transparent; }
        #dialogCloseButton {
            color: white;
            background: transparent;
            border: none;
            border-radius: 0;
            padding: 0;
            font-size: 17px;
        }
        #dialogCloseButton:hover { background: #e34d59; }
        #dialogSizeGrip { background: transparent; width: 18px; height: 18px; }
        #loginCard { background: white; border: 1px solid #dedede; border-radius: 12px; }
        #brandTitle { font-size: 30px; font-weight: 700; color: #1f1f1f; }
        #subtleLabel { color: #8a8a8a; }
        QLineEdit, QPlainTextEdit {
            background: white;
            border: 1px solid #d5d5d5;
            border-radius: 5px;
            padding: 9px 11px;
            selection-background-color: #07c160;
        }
        QLineEdit { min-height: 22px; }
        QLineEdit:focus, QPlainTextEdit:focus { border-color: #07c160; }
        QPushButton {
            background: #f2f2f2;
            border: none;
            border-radius: 4px;
            padding: 8px 14px;
        }
        QPushButton:hover { background: #e7e7e7; }
        QPushButton:pressed { background: #dddddd; }
        QPushButton:disabled { color: #b7b7b7; background: #eeeeee; }
        #primaryButton { color: white; background: #07c160; font-weight: 600; }
        #primaryButton:hover { background: #06ad56; }
        #primaryButton:pressed { background: #059c4e; }
        #navigation { background: #2e2e2e; }
        #avatarButton { background: transparent; border: none; padding: 2px; }
        #avatarButton:hover { background: #3b3b3b; }
        #navButton { color: #b8b8b8; background: transparent; border-radius: 4px; padding: 8px 2px; }
        #navButton:hover { color: white; background: #383838; }
        #navButton:checked { color: #07c160; background: #383838; }
        #connectionLabel { color: #8d8d8d; font-size: 11px; }
        #entityPanel { background: #f7f7f7; border-right: 1px solid #d6d6d6; }
        #listSearch { background: #eaeaea; border: none; border-radius: 4px; padding: 7px 10px; }
        #listSearch:focus { background: white; border: 1px solid #d2d2d2; }
        #listToolButton { background: #eaeaea; font-size: 18px; padding: 0; }
        #listToolButton:hover { background: #dedede; }
        #entityList, #messageList { border: none; background: transparent; outline: none; }
        #entityList::item { border: none; margin: 0; padding: 0; }
        #entityList::item:hover { background: #e9e9e9; }
        #entityList::item:selected { color: #222222; background: #d9d9d9; }
        #entityRow { background: transparent; }
        #entityAvatar { background: transparent; }
        #entityTitle { color: #1f1f1f; font-size: 15px; font-weight: 500; }
        #entitySubtitle { color: #8b8b8b; font-size: 12px; }
        #unreadBadge { color: white; background: #fa5151; border-radius: 10px; font-size: 10px; padding: 0 5px; }
        #conversationPanel { background: #ededed; }
        #titleBar { background: #f5f5f5; border-bottom: 1px solid #d7d7d7; }
        #conversationTitle { font-size: 18px; font-weight: 600; color: #1f1f1f; }
        #headerButton { background: transparent; color: #555555; padding: 8px 11px; }
        #headerButton:hover { background: #e7e7e7; }
        #messageList { background: #ededed; padding-top: 10px; }
        #messageList::item { background: transparent; border: none; margin: 0; padding: 0; }
        #messageList::item:selected { background: transparent; }
        #messageMeta { color: #999999; font-size: 11px; }
        #messageAvatar { background: transparent; }
        #messageBubbleMine { background: #95ec69; border: none; border-radius: 6px; }
        #messageBubbleOther { background: white; border: none; border-radius: 6px; }
        #messageText { color: #181818; font-size: 14px; background: transparent; }
        #messageImage { background: transparent; }
        #messageFileTitle { color: #222222; font-size: 14px; font-weight: 500; background: transparent; }
        #messageFileMeta { color: #8a8a8a; font-size: 11px; background: transparent; }
        #composerPanel { background: white; border-top: 1px solid #d7d7d7; }
        #composerToolButton { background: transparent; color: #444444; padding: 5px 10px; }
        #composerToolButton:hover { background: #f0f0f0; }
        #messageComposer { border: none; border-radius: 0; padding: 5px 2px; background: white; font-size: 14px; }
        #statusBar { color: #999999; background: white; padding-left: 22px; border: none; }
        QSplitter::handle { background: #d6d6d6; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { padding: 9px 18px; }
        QTabBar::tab:selected { color: #07c160; border-bottom: 2px solid #07c160; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
        QScrollBar::handle:vertical { background: #c8c8c8; border-radius: 4px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: #aaaaaa; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )"));
}

void AppWindow::wire_network_events() {
    connect(&gateway_, &GatewayClient::websocketConnected, this, [this] {
        connection_label_->setText(QStringLiteral("在线"));
        show_status(QStringLiteral("已连接"));
    });
    connect(&gateway_, &GatewayClient::websocketError, this, [this](const QString& error) {
        connection_label_->setText(QStringLiteral("连接异常"));
        show_status(QStringLiteral("连接异常：") + error, 6000);
    });
    connect(&gateway_, &GatewayClient::websocketDisconnected, this, [this] {
        connection_label_->setText(QStringLiteral("已断开"));
        if (intentional_logout_) {
            intentional_logout_ = false;
            return;
        }
        if (!session_id_.isEmpty()) {
            logout(true);
        }
    });
    connect(&gateway_, &GatewayClient::notificationReceived, this, &AppWindow::handle_notification);
}

bool AppWindow::configure_endpoints() {
    const QUrl http(http_url_edit_->text().trimmed());
    const QUrl websocket(websocket_url_edit_->text().trimmed());
    const bool valid_http = http.isValid() &&
        (http.scheme() == QStringLiteral("http") || http.scheme() == QStringLiteral("https")) &&
        !http.host().isEmpty();
    const bool valid_websocket = websocket.isValid() &&
        (websocket.scheme() == QStringLiteral("ws") || websocket.scheme() == QStringLiteral("wss")) &&
        !websocket.host().isEmpty();
    if (!valid_http || !valid_websocket) {
        QMessageBox::warning(this, QStringLiteral("连接失败"), QStringLiteral("连接设置无效，请重新启动客户端。"));
        return false;
    }
    gateway_.configure(http, websocket);
    return true;
}

void AppWindow::set_login_busy(bool busy) {
    for (auto* button : {
             account_login_button_, account_register_button_, phone_code_button_,
             phone_login_button_, phone_register_button_}) {
        button->setEnabled(!busy);
    }
    login_status_->setText(busy ? QStringLiteral("正在连接…") : QStringLiteral("请输入账号，或先注册新用户"));
}

void AppWindow::account_login() {
    if (!configure_endpoints()) {
        return;
    }
    if (username_edit_->text().trimmed().isEmpty() || password_edit_->text().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("登录"), QStringLiteral("用户名和密码不能为空。"));
        return;
    }
    set_login_busy(true);
    ahwei_im::UserLoginReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_nickname(utf8(username_edit_->text().trimmed()));
    request.set_password(utf8(password_edit_->text()));
    gateway_.post<ahwei_im::UserLoginReq, ahwei_im::UserLoginRsp>(
        QStringLiteral("/service/user/username_login"), request,
        [this](const ahwei_im::UserLoginRsp& response, const QString& transport_error) {
            set_login_busy(false);
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("登录失败"), error);
                return;
            }
            enter_chat(text(response.login_session_id()));
        });
}

void AppWindow::account_register() {
    if (!configure_endpoints()) {
        return;
    }
    if (username_edit_->text().trimmed().isEmpty() || password_edit_->text().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("注册"), QStringLiteral("用户名和密码不能为空。"));
        return;
    }
    set_login_busy(true);
    ahwei_im::UserRegisterReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_nickname(utf8(username_edit_->text().trimmed()));
    request.set_password(utf8(password_edit_->text()));
    gateway_.post<ahwei_im::UserRegisterReq, ahwei_im::UserRegisterRsp>(
        QStringLiteral("/service/user/username_register"), request,
        [this](const ahwei_im::UserRegisterRsp& response, const QString& transport_error) {
            set_login_busy(false);
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("注册失败"), error);
                return;
            }
            QMessageBox::information(this, QStringLiteral("注册成功"), QStringLiteral("账号已创建，现在可以登录。"));
        });
}

void AppWindow::request_phone_code() {
    if (!configure_endpoints()) {
        return;
    }
    ahwei_im::PhoneVerifyCodeReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_phone_number(utf8(phone_edit_->text().trimmed()));
    set_login_busy(true);
    gateway_.post<ahwei_im::PhoneVerifyCodeReq, ahwei_im::PhoneVerifyCodeRsp>(
        QStringLiteral("/service/user/get_phone_verify_code"), request,
        [this](const ahwei_im::PhoneVerifyCodeRsp& response, const QString& transport_error) {
            set_login_busy(false);
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取验证码失败"), error);
                return;
            }
            phone_code_id_ = response.verify_code_id();
            QMessageBox::information(
                this, QStringLiteral("验证码已生成"),
                QStringLiteral("验证码已生成，有效期 5 分钟且只能使用一次。"));
        });
}

void AppWindow::phone_login(bool registration) {
    if (!configure_endpoints()) {
        return;
    }
    if (phone_code_id_.empty() || phone_code_edit_->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("手机验证"), QStringLiteral("请先获取并输入验证码。"));
        return;
    }
    set_login_busy(true);
    if (registration) {
        ahwei_im::PhoneRegisterReq request;
        request.set_request_id(utf8(GatewayClient::make_request_id()));
        request.set_phone_number(utf8(phone_edit_->text().trimmed()));
        request.set_verify_code_id(phone_code_id_);
        request.set_verify_code(utf8(phone_code_edit_->text().trimmed()));
        gateway_.post<ahwei_im::PhoneRegisterReq, ahwei_im::PhoneRegisterRsp>(
            QStringLiteral("/service/user/phone_register"), request,
            [this](const ahwei_im::PhoneRegisterRsp& response, const QString& transport_error) {
                set_login_busy(false);
                const QString error = response_error(response, transport_error);
                if (!error.isEmpty()) {
                    show_error(QStringLiteral("手机注册失败"), error);
                    return;
                }
                phone_code_id_.clear();
                phone_code_edit_->clear();
                QMessageBox::information(this, QStringLiteral("注册成功"), QStringLiteral("请重新获取验证码后登录。"));
            });
        return;
    }
    ahwei_im::PhoneLoginReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_phone_number(utf8(phone_edit_->text().trimmed()));
    request.set_verify_code_id(phone_code_id_);
    request.set_verify_code(utf8(phone_code_edit_->text().trimmed()));
    gateway_.post<ahwei_im::PhoneLoginReq, ahwei_im::PhoneLoginRsp>(
        QStringLiteral("/service/user/phone_login"), request,
        [this](const ahwei_im::PhoneLoginRsp& response, const QString& transport_error) {
            set_login_busy(false);
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("手机登录失败"), error);
                return;
            }
            phone_code_id_.clear();
            enter_chat(text(response.login_session_id()));
        });
}

void AppWindow::enter_chat(const QString& session_id) {
    session_id_ = session_id;
    intentional_logout_ = false;
    current_session_id_.clear();
    friends_.clear();
    sessions_.clear();
    applications_.clear();
    messages_.clear();
    unread_.clear();
    group_avatars_.clear();
    group_avatar_requests_.clear();
    pages_->setCurrentWidget(chat_page_);
    connection_label_->setText(QStringLiteral("连接中"));
    gateway_.open_websocket(session_id_);
    refresh_self();
    refresh_friends();
    refresh_sessions();
    refresh_applications();
    switch_list_mode(ListMode::SESSIONS);
}

void AppWindow::logout(bool show_message) {
    intentional_logout_ = true;
    gateway_.close_websocket();
    session_id_.clear();
    current_session_id_.clear();
    pages_->setCurrentWidget(login_page_);
    password_edit_->clear();
    login_status_->setText(show_message
        ? QStringLiteral("连接已断开，请重新登录")
        : QStringLiteral("已退出登录"));
    if (show_message) {
        QMessageBox::warning(this, QStringLiteral("连接断开"), QStringLiteral("连接已断开，请重新登录。"));
    }
}

void AppWindow::switch_list_mode(ListMode mode) {
    list_mode_ = mode;
    sessions_button_->setChecked(mode == ListMode::SESSIONS);
    friends_button_->setChecked(mode == ListMode::FRIENDS);
    applications_button_->setChecked(mode == ListMode::APPLICATIONS);
    list_action_button_->setText(mode == ListMode::APPLICATIONS ? QStringLiteral("↻") : QStringLiteral("＋"));
    if (mode == ListMode::SESSIONS) {
        render_sessions();
    } else if (mode == ListMode::FRIENDS) {
        render_friends();
    } else {
        render_applications();
    }
}

void AppWindow::refresh_visible_list() {
    if (list_mode_ == ListMode::SESSIONS) {
        refresh_sessions();
    } else if (list_mode_ == ListMode::FRIENDS) {
        refresh_friends();
    } else {
        refresh_applications();
    }
}

void AppWindow::refresh_self() {
    ahwei_im::GetUserInfoReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    gateway_.post<ahwei_im::GetUserInfoReq, ahwei_im::GetUserInfoRsp>(
        QStringLiteral("/service/user/get_user_info"), request,
        [this](const ahwei_im::GetUserInfoRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取个人资料失败"), error);
                return;
            }
            self_.CopyFrom(response.user_info());
            profile_button_->setIcon(avatar_icon(self_));
            profile_button_->setToolTip(user_display_name(self_));
        });
}

void AppWindow::refresh_friends() {
    ahwei_im::GetFriendListReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    gateway_.post<ahwei_im::GetFriendListReq, ahwei_im::GetFriendListRsp>(
        QStringLiteral("/service/friend/get_friend_list"), request,
        [this](const ahwei_im::GetFriendListRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取好友列表失败"), error);
                return;
            }
            friends_.assign(response.friend_list().begin(), response.friend_list().end());
            if (list_mode_ == ListMode::FRIENDS) {
                render_friends();
            }
        });
}

void AppWindow::refresh_sessions() {
    ahwei_im::GetChatSessionListReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    gateway_.post<ahwei_im::GetChatSessionListReq, ahwei_im::GetChatSessionListRsp>(
        QStringLiteral("/service/friend/get_chat_session_list"), request,
        [this](const ahwei_im::GetChatSessionListRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取会话列表失败"), error);
                return;
            }
            sessions_.assign(
                response.chat_session_info_list().begin(),
                response.chat_session_info_list().end());
            if (list_mode_ == ListMode::SESSIONS) {
                render_sessions();
            }
            refresh_group_avatars();
        });
}

void AppWindow::refresh_group_avatars() {
    QSet<QString> active_groups;
    for (const auto& session : sessions_) {
        if (!session.single_chat_friend_id().empty()) {
            continue;
        }
        const QString group_id = text(session.chat_session_id());
        active_groups.insert(group_id);
        if (group_avatar_requests_.contains(group_id)) {
            continue;
        }
        group_avatar_requests_.insert(group_id);

        ahwei_im::GetChatSessionMemberReq request;
        request.set_request_id(utf8(GatewayClient::make_request_id()));
        request.set_session_id(utf8(session_id_));
        request.set_chat_session_id(session.chat_session_id());
        gateway_.post<
            ahwei_im::GetChatSessionMemberReq,
            ahwei_im::GetChatSessionMemberRsp>(
            QStringLiteral("/service/friend/get_chat_session_member"), request,
            [this, group_id](
                const ahwei_im::GetChatSessionMemberRsp& response,
                const QString& transport_error) {
                group_avatar_requests_.remove(group_id);
                const QString error = response_error(response, transport_error);
                if (!error.isEmpty()) {
                    return;
                }
                group_avatars_.insert(
                    group_id, group_avatar_icon(response.member_info_list()));
                if (list_mode_ == ListMode::SESSIONS) {
                    render_sessions();
                }
            });
    }

    for (auto iterator = group_avatars_.begin(); iterator != group_avatars_.end();) {
        if (!active_groups.contains(iterator.key())) {
            iterator = group_avatars_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void AppWindow::refresh_applications() {
    ahwei_im::GetPendingFriendEventListReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    gateway_.post<
        ahwei_im::GetPendingFriendEventListReq,
        ahwei_im::GetPendingFriendEventListRsp>(
        QStringLiteral("/service/friend/get_pending_friend_events"), request,
        [this](const ahwei_im::GetPendingFriendEventListRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取好友申请失败"), error);
                return;
            }
            applications_.clear();
            for (const auto& event : response.event()) {
                applications_.push_back(PendingApplication{event.event_id(), event.sender()});
            }
            if (list_mode_ == ListMode::APPLICATIONS) {
                render_applications();
            }
        });
}

void AppWindow::render_sessions() {
    entity_list_->clear();
    for (const auto& session : sessions_) {
        QString summary = session.has_prev_message()
            ? message_summary(session.prev_message())
            : QStringLiteral("暂无消息");
        const QString id = text(session.chat_session_id());
        const int unread = unread_.value(id, 0);
        const QString title = session_display_name(session);
        QIcon icon = session_avatar_icon(session);
        if (session.single_chat_friend_id().empty() && group_avatars_.contains(id)) {
            icon = group_avatars_.value(id);
        }
        auto* item = new QListWidgetItem;
        item->setData(kEntityIdRole, id);
        item->setToolTip(QStringLiteral("双击打开会话"));
        entity_list_->addItem(item);
        install_entity_row(
            entity_list_, item, icon,
            title, summary, unread);
    }
    filter_visible_list(filter_edit_->text());
}

void AppWindow::render_friends() {
    entity_list_->clear();
    for (const auto& friend_info : friends_) {
        const QString subtitle = friend_info.description().empty()
            ? QStringLiteral("暂无签名")
            : text(friend_info.description());
        auto* item = new QListWidgetItem;
        item->setData(kEntityIdRole, text(friend_info.user_id()));
        item->setToolTip(QStringLiteral("双击进入会话，右键管理好友"));
        entity_list_->addItem(item);
        install_entity_row(
            entity_list_, item, avatar_icon(friend_info),
            user_display_name(friend_info), subtitle);
    }
    filter_visible_list(filter_edit_->text());
}

void AppWindow::render_applications() {
    entity_list_->clear();
    for (const auto& application : applications_) {
        auto* item = new QListWidgetItem;
        item->setData(kEntityIdRole, text(application.event_id));
        item->setData(kEntityAuxRole, text(application.sender.user_id()));
        item->setToolTip(QStringLiteral("双击处理好友申请"));
        entity_list_->addItem(item);
        install_entity_row(
            entity_list_, item, avatar_icon(application.sender),
            user_display_name(application.sender), QStringLiteral("请求添加你为好友"));
    }
    filter_visible_list(filter_edit_->text());
}

void AppWindow::filter_visible_list(const QString& keyword) {
    for (int index = 0; index < entity_list_->count(); ++index) {
        auto* item = entity_list_->item(index);
        item->setHidden(!item->data(kEntityFilterRole).toString().contains(
            keyword, Qt::CaseInsensitive));
    }
}

void AppWindow::handle_list_item(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    const std::string id = utf8(item->data(kEntityIdRole).toString());
    if (list_mode_ == ListMode::SESSIONS) {
        select_session(id);
        return;
    }
    if (list_mode_ == ListMode::FRIENDS) {
        const auto iterator = std::find_if(
            sessions_.begin(), sessions_.end(), [&id](const auto& session) {
                return session.single_chat_friend_id() == id;
            });
        if (iterator == sessions_.end()) {
            show_status(QStringLiteral("未找到该好友的单聊会话，请刷新会话列表"));
            refresh_sessions();
            return;
        }
        switch_list_mode(ListMode::SESSIONS);
        select_session(iterator->chat_session_id());
        return;
    }
    const auto iterator = std::find_if(
        applications_.begin(), applications_.end(), [&id](const auto& application) {
            return application.event_id == id;
        });
    if (iterator != applications_.end()) {
        process_application(*iterator);
    }
}

void AppWindow::show_list_context_menu(const QPoint& position) {
    auto* item = entity_list_->itemAt(position);
    if (!item) {
        return;
    }
    QMenu menu(this);
    if (list_mode_ == ListMode::FRIENDS) {
        QAction* chat = menu.addAction(QStringLiteral("打开会话"));
        QAction* remove = menu.addAction(QStringLiteral("删除好友"));
        QAction* selected = menu.exec(entity_list_->viewport()->mapToGlobal(position));
        if (selected == chat) {
            handle_list_item(item);
        } else if (selected == remove) {
            remove_friend(utf8(item->data(kEntityIdRole).toString()));
        }
    } else if (list_mode_ == ListMode::APPLICATIONS) {
        QAction* process = menu.addAction(QStringLiteral("处理申请"));
        if (menu.exec(entity_list_->viewport()->mapToGlobal(position)) == process) {
            handle_list_item(item);
        }
    } else {
        QAction* open = menu.addAction(QStringLiteral("打开会话"));
        QAction* members = menu.addAction(QStringLiteral("查看成员"));
        QAction* selected = menu.exec(entity_list_->viewport()->mapToGlobal(position));
        if (selected == open) {
            handle_list_item(item);
        } else if (selected == members) {
            select_session(utf8(item->data(kEntityIdRole).toString()));
            show_session_members();
        }
    }
}

void AppWindow::select_session(const std::string& chat_session_id) {
    const auto iterator = std::find_if(
        sessions_.begin(), sessions_.end(), [&chat_session_id](const auto& session) {
            return session.chat_session_id() == chat_session_id;
        });
    if (iterator == sessions_.end()) {
        return;
    }
    current_session_id_ = chat_session_id;
    unread_[text(chat_session_id)] = 0;
    conversation_title_->setText(session_display_name(*iterator));
    members_button_->setEnabled(true);
    history_button_->setEnabled(true);
    refresh_recent_messages(chat_session_id);
    render_sessions();
}

void AppWindow::refresh_recent_messages(const std::string& chat_session_id) {
    ahwei_im::GetRecentMsgReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_chat_session_id(chat_session_id);
    request.set_msg_count(50);
    request.set_cur_time(QDateTime::currentSecsSinceEpoch());
    gateway_.post<ahwei_im::GetRecentMsgReq, ahwei_im::GetRecentMsgRsp>(
        QStringLiteral("/service/message_storage/get_recent"), request,
        [this, chat_session_id](const ahwei_im::GetRecentMsgRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取最近消息失败"), error);
                return;
            }
            auto& target = messages_[text(chat_session_id)];
            target.assign(response.msg_list().begin(), response.msg_list().end());
            if (current_session_id_ == chat_session_id) {
                render_messages();
            }
        });
}

void AppWindow::render_messages() {
    message_list_->clear();
    const auto iterator = messages_.constFind(text(current_session_id_));
    if (iterator == messages_.constEnd()) {
        return;
    }
    for (const auto& message : iterator.value()) {
        append_message_item(message);
    }
    message_list_->scrollToBottom();
}

void AppWindow::append_message_item(const ahwei_im::MessageInfo& message) {
    const QString sender = message.has_sender()
        ? user_display_name(message.sender())
        : QStringLiteral("未知用户");
    const QString timestamp = QDateTime::fromSecsSinceEpoch(message.timestamp())
                                  .toString(QStringLiteral("MM-dd HH:mm:ss"));
    const bool mine = message.has_sender() && message.sender().user_id() == self_.user_id();

    auto* item = new QListWidgetItem;
    const std::string serialized = message.SerializeAsString();
    item->setData(kSerializedMessageRole, QByteArray(
        serialized.data(), static_cast<int>(serialized.size())));
    message_list_->addItem(item);

    auto* row = new QWidget;
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(24, 5, 24, 5);
    row_layout->setSpacing(10);

    const QIcon sender_icon = message.has_sender()
        ? avatar_icon(message.sender())
        : QIcon(QStringLiteral(":/chat/default-avatar.png"));
    auto* avatar = make_icon_label(sender_icon, 40, QStringLiteral("messageAvatar"));

    auto* body = new QWidget;
    body->setMaximumWidth(540);
    body->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    auto* body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(4);

    auto* meta = new QLabel(QStringLiteral("%1  %2").arg(sender, timestamp));
    meta->setObjectName(QStringLiteral("messageMeta"));
    meta->setAlignment(mine ? Qt::AlignRight : Qt::AlignLeft);
    body_layout->addWidget(meta);

    auto* bubble = new QFrame;
    bubble->setObjectName(mine
        ? QStringLiteral("messageBubbleMine")
        : QStringLiteral("messageBubbleOther"));
    bubble->setMaximumWidth(500);
    auto* bubble_layout = new QVBoxLayout(bubble);
    bubble_layout->setContentsMargins(12, 9, 12, 9);
    bubble_layout->setSpacing(0);

    if (message.has_message() &&
        message.message().message_type() == ahwei_im::IMAGE &&
        message.message().has_image_message()) {
        QPixmap image;
        image.loadFromData(bytes(message.message().image_message().image_content()));
        auto* image_label = new QLabel;
        image_label->setObjectName(QStringLiteral("messageImage"));
        image_label->setAlignment(Qt::AlignCenter);
        image_label->setPixmap(image.isNull()
            ? QIcon(QStringLiteral(":/chat/image.png")).pixmap(96, 96)
            : image.scaled(240, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        image_label->setToolTip(QStringLiteral("双击预览图片"));
        bubble_layout->setContentsMargins(6, 6, 6, 6);
        bubble_layout->addWidget(image_label);
    } else if (message.has_message() &&
               message.message().message_type() == ahwei_im::FILE &&
               message.message().has_file_message()) {
        const auto& file = message.message().file_message();
        bubble->setMinimumWidth(290);
        auto* file_row = new QHBoxLayout;
        file_row->setContentsMargins(0, 0, 0, 0);
        file_row->setSpacing(12);
        auto* file_text = new QVBoxLayout;
        file_text->setContentsMargins(0, 2, 0, 2);
        file_text->setSpacing(5);
        auto* file_name = new QLabel(text(file.file_name()));
        file_name->setObjectName(QStringLiteral("messageFileTitle"));
        file_name->setWordWrap(true);
        auto* file_meta = new QLabel(
            file_size_text(file.file_size()) + QStringLiteral(" · 双击保存"));
        file_meta->setObjectName(QStringLiteral("messageFileMeta"));
        file_text->addWidget(file_name);
        file_text->addWidget(file_meta);
        file_row->addLayout(file_text, 1);
        file_row->addWidget(make_icon_label(
            QIcon(QStringLiteral(":/chat/file.png")), 42,
            QStringLiteral("messageImage")));
        bubble_layout->addLayout(file_row);
    } else {
        auto* content = new QLabel(message_summary(message));
        content->setObjectName(QStringLiteral("messageText"));
        content->setTextFormat(Qt::PlainText);
        content->setWordWrap(true);
        content->setMaximumWidth(450);
        bubble_layout->addWidget(content);
    }

    body_layout->addWidget(
        bubble, 0, mine ? Qt::AlignRight : Qt::AlignLeft);
    if (mine) {
        row_layout->addStretch();
        row_layout->addWidget(body, 0, Qt::AlignTop);
        row_layout->addWidget(avatar, 0, Qt::AlignTop);
    } else {
        row_layout->addWidget(avatar, 0, Qt::AlignTop);
        row_layout->addWidget(body, 0, Qt::AlignTop);
        row_layout->addStretch();
    }

    row->adjustSize();
    item->setSizeHint(QSize(0, std::max(72, row->sizeHint().height() + 8)));
    message_list_->setItemWidget(item, row);
}

void AppWindow::open_message_attachment(QListWidgetItem* item) {
    if (!item) {
        return;
    }
    const QByteArray serialized = item->data(kSerializedMessageRole).toByteArray();
    ahwei_im::MessageInfo message;
    if (!message.ParseFromArray(serialized.constData(), serialized.size()) || !message.has_message()) {
        return;
    }
    const auto& content = message.message();
    if (content.message_type() == ahwei_im::STRING) {
        return;
    }
    std::string file_id;
    std::string suggested_name;
    std::string file_content;
    bool image = false;
    if (content.message_type() == ahwei_im::IMAGE && content.has_image_message()) {
        file_id = content.image_message().file_id();
        file_content = content.image_message().image_content();
        suggested_name = "image.png";
        image = true;
    } else if (content.message_type() == ahwei_im::FILE && content.has_file_message()) {
        file_id = content.file_message().file_id();
        file_content = content.file_message().file_contents();
        suggested_name = content.file_message().file_name();
    } else if (content.message_type() == ahwei_im::SPEECH && content.has_speech_message()) {
        file_id = content.speech_message().file_id();
        file_content = content.speech_message().file_contents();
        suggested_name = "speech.pcm";
    } else {
        return;
    }

    auto consume = [this, image, suggested_name](const std::string& downloaded) {
        if (image) {
            QPixmap pixmap;
            pixmap.loadFromData(bytes(downloaded));
            if (pixmap.isNull()) {
                show_error(QStringLiteral("打开图片失败"), QStringLiteral("图片数据格式无法识别"));
                return;
            }
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("图片预览"));
            dialog.resize(720, 560);
            auto* layout = new QVBoxLayout(&dialog);
            auto* label = new QLabel;
            label->setAlignment(Qt::AlignCenter);
            label->setPixmap(pixmap.scaled(680, 510, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            layout->addWidget(label);
            dialog.exec();
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存附件"), text(suggested_name));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes(downloaded)) < 0) {
            show_error(QStringLiteral("保存附件失败"), file.errorString());
            return;
        }
        show_status(QStringLiteral("附件已保存到 %1").arg(path));
    };

    if (!file_content.empty()) {
        consume(file_content);
        return;
    }
    if (file_id.empty()) {
        show_error(QStringLiteral("下载附件失败"), QStringLiteral("附件信息不完整"));
        return;
    }
    ahwei_im::GetSingleFileReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_file_id(file_id);
    gateway_.post<ahwei_im::GetSingleFileReq, ahwei_im::GetSingleFileRsp>(
        QStringLiteral("/service/file/get_single_file"), request,
        [this, consume = std::move(consume)](
            const ahwei_im::GetSingleFileRsp& response,
            const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("下载附件失败"), error);
                return;
            }
            consume(response.file_data().file_content());
        });
}

void AppWindow::send_text_message() {
    const QString content = composer_->toPlainText().trimmed();
    if (content.isEmpty()) {
        return;
    }
    ahwei_im::MessageContent message;
    message.set_message_type(ahwei_im::STRING);
    message.mutable_string_message()->set_content(utf8(content));
    composer_->clear();
    send_content(message);
}

void AppWindow::send_image_message() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择图片"),
        {},
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.gif);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        show_error(QStringLiteral("读取图片失败"), file.errorString());
        return;
    }
    const QByteArray content = file.readAll();
    ahwei_im::MessageContent message;
    message.set_message_type(ahwei_im::IMAGE);
    message.mutable_image_message()->set_image_content(
        content.constData(), static_cast<std::size_t>(content.size()));
    send_content(message);
}

void AppWindow::send_file_message() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        show_error(QStringLiteral("读取文件失败"), file.errorString());
        return;
    }
    const QByteArray content = file.readAll();
    const QFileInfo info(file);
    ahwei_im::MessageContent message;
    message.set_message_type(ahwei_im::FILE);
    auto* attachment = message.mutable_file_message();
    attachment->set_file_name(utf8(info.fileName()));
    attachment->set_file_size(content.size());
    attachment->set_file_contents(
        content.constData(), static_cast<std::size_t>(content.size()));
    send_content(message);
}

void AppWindow::send_content(const ahwei_im::MessageContent& content) {
    if (current_session_id_.empty()) {
        show_status(QStringLiteral("请先选择一个会话"));
        return;
    }
    send_button_->setEnabled(false);
    ahwei_im::NewMessageReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_chat_session_id(current_session_id_);
    request.mutable_message()->CopyFrom(content);
    const std::string sent_session_id = current_session_id_;
    gateway_.post<ahwei_im::NewMessageReq, ahwei_im::NewMessageRsp>(
        QStringLiteral("/service/message_transmit/new_message"), request,
        [this, sent_session_id](const ahwei_im::NewMessageRsp& response, const QString& transport_error) {
            send_button_->setEnabled(true);
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("发送消息失败"), error);
                return;
            }
            show_status(QStringLiteral("消息已发送"));
            refresh_recent_messages(sent_session_id);
            refresh_sessions();
        });
}

void AppWindow::show_add_friend_dialog() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("搜索并添加好友"));
    dialog->resize(520, 520);
    auto* layout = new QVBoxLayout(dialog);
    auto* search_row = new QHBoxLayout;
    auto* search_edit = new QLineEdit;
    search_edit->setPlaceholderText(QStringLiteral("输入用户名、签名或手机号"));
    auto* search_button = new QPushButton(QStringLiteral("搜索"));
    search_button->setObjectName(QStringLiteral("primaryButton"));
    search_row->addWidget(search_edit, 1);
    search_row->addWidget(search_button);
    auto* results_widget = new QListWidget;
    auto* add_button = new QPushButton(QStringLiteral("发送好友申请"));
    auto results = std::make_shared<std::vector<ahwei_im::UserInfo>>();
    layout->addLayout(search_row);
    layout->addWidget(results_widget, 1);
    layout->addWidget(add_button);
    QPointer<QDialog> guarded_dialog(dialog);

    auto search = [this, guarded_dialog, search_edit, results_widget, results] {
        if (!guarded_dialog) {
            return;
        }
        ahwei_im::FriendSearchReq request;
        request.set_request_id(utf8(GatewayClient::make_request_id()));
        request.set_session_id(utf8(session_id_));
        request.set_search_key(utf8(search_edit->text().trimmed()));
        gateway_.post<ahwei_im::FriendSearchReq, ahwei_im::FriendSearchRsp>(
            QStringLiteral("/service/friend/search_friend"), request,
            [this, guarded_dialog, results_widget, results](
                const ahwei_im::FriendSearchRsp& response,
                const QString& transport_error) {
                if (!guarded_dialog) {
                    return;
                }
                const QString error = response_error(response, transport_error);
                if (!error.isEmpty()) {
                    show_error(QStringLiteral("搜索用户失败"), error);
                    return;
                }
                results->assign(response.user_info().begin(), response.user_info().end());
                results_widget->clear();
                for (const auto& found : *results) {
                    auto* item = new QListWidgetItem(
                        avatar_icon(found),
                        user_display_name(found) + QStringLiteral("\n") + text(found.description()));
                    item->setData(kEntityIdRole, text(found.user_id()));
                    results_widget->addItem(item);
                }
            });
    };
    connect(search_button, &QPushButton::clicked, dialog, search);
    connect(search_edit, &QLineEdit::returnPressed, dialog, search);
    connect(add_button, &QPushButton::clicked, dialog, [this, guarded_dialog, results_widget] {
        auto* item = results_widget->currentItem();
        if (!item || !guarded_dialog) {
            return;
        }
        ahwei_im::FriendAddReq request;
        request.set_request_id(utf8(GatewayClient::make_request_id()));
        request.set_session_id(utf8(session_id_));
        request.set_respondent_id(utf8(item->data(kEntityIdRole).toString()));
        gateway_.post<ahwei_im::FriendAddReq, ahwei_im::FriendAddRsp>(
            QStringLiteral("/service/friend/add_friend_apply"), request,
            [this, guarded_dialog](const ahwei_im::FriendAddRsp& response, const QString& transport_error) {
                const QString error = response_error(response, transport_error);
                if (!error.isEmpty()) {
                    show_error(QStringLiteral("发送好友申请失败"), error);
                    return;
                }
                show_status(QStringLiteral("好友申请已发送"));
                if (guarded_dialog) {
                    guarded_dialog->accept();
                }
            });
    });
    dialog->show();
}

void AppWindow::process_application(const PendingApplication& application) {
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("好友申请"));
    box.setText(QStringLiteral("%1 请求添加你为好友").arg(user_display_name(application.sender)));
    auto* accept = box.addButton(QStringLiteral("同意"), QMessageBox::AcceptRole);
    auto* reject = box.addButton(QStringLiteral("拒绝"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != accept && box.clickedButton() != reject) {
        return;
    }
    const bool agree = box.clickedButton() == accept;
    ahwei_im::FriendAddProcessReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_notify_event_id(application.event_id);
    request.set_apply_user_id(application.sender.user_id());
    request.set_agree(agree);
    gateway_.post<ahwei_im::FriendAddProcessReq, ahwei_im::FriendAddProcessRsp>(
        QStringLiteral("/service/friend/add_friend_process"), request,
        [this, agree](const ahwei_im::FriendAddProcessRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("处理好友申请失败"), error);
                return;
            }
            show_status(agree ? QStringLiteral("已添加好友") : QStringLiteral("已拒绝好友申请"));
            refresh_applications();
            refresh_friends();
            refresh_sessions();
        });
}

void AppWindow::remove_friend(const std::string& user_id) {
    if (QMessageBox::question(this, QStringLiteral("删除好友"), QStringLiteral("确定删除该好友及对应单聊会话吗？")) != QMessageBox::Yes) {
        return;
    }
    ahwei_im::FriendRemoveReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_peer_id(user_id);
    gateway_.post<ahwei_im::FriendRemoveReq, ahwei_im::FriendRemoveRsp>(
        QStringLiteral("/service/friend/remove_friend"), request,
        [this](const ahwei_im::FriendRemoveRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("删除好友失败"), error);
                return;
            }
            current_session_id_.clear();
            conversation_title_->setText(QStringLiteral("请选择一个会话"));
            message_list_->clear();
            refresh_friends();
            refresh_sessions();
        });
}

void AppWindow::show_create_group_dialog() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("创建群聊"));
    dialog->resize(460, 540);
    auto* layout = new QVBoxLayout(dialog);
    auto* name = new QLineEdit;
    name->setPlaceholderText(QStringLiteral("群聊名称"));
    auto* list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const auto& friend_info : friends_) {
        auto* item = new QListWidgetItem(avatar_icon(friend_info), user_display_name(friend_info));
        item->setData(kEntityIdRole, text(friend_info.user_id()));
        list->addItem(item);
    }
    auto* create = new QPushButton(QStringLiteral("创建群聊"));
    create->setObjectName(QStringLiteral("primaryButton"));
    layout->addWidget(name);
    layout->addWidget(new QLabel(QStringLiteral("选择至少一位好友（创建者会自动加入）")));
    layout->addWidget(list, 1);
    layout->addWidget(create);
    QPointer<QDialog> guarded_dialog(dialog);
    connect(create, &QPushButton::clicked, dialog, [this, guarded_dialog, name, list] {
        const auto selected = list->selectedItems();
        if (name->text().trimmed().isEmpty() || selected.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("创建群聊"), QStringLiteral("请输入群名并选择至少一位好友。"));
            return;
        }
        ahwei_im::ChatSessionCreateReq request;
        request.set_request_id(utf8(GatewayClient::make_request_id()));
        request.set_session_id(utf8(session_id_));
        request.set_chat_session_name(utf8(name->text().trimmed()));
        request.add_member_id_list(self_.user_id());
        for (auto* item : selected) {
            request.add_member_id_list(utf8(item->data(kEntityIdRole).toString()));
        }
        gateway_.post<ahwei_im::ChatSessionCreateReq, ahwei_im::ChatSessionCreateRsp>(
            QStringLiteral("/service/friend/create_chat_session"), request,
            [this, guarded_dialog](const ahwei_im::ChatSessionCreateRsp& response, const QString& transport_error) {
                const QString error = response_error(response, transport_error);
                if (!error.isEmpty()) {
                    show_error(QStringLiteral("创建群聊失败"), error);
                    return;
                }
                show_status(QStringLiteral("群聊已创建"));
                refresh_sessions();
                if (guarded_dialog) {
                    guarded_dialog->accept();
                }
            });
    });
    dialog->show();
}

void AppWindow::show_profile_dialog() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("个人资料"));
    dialog->resize(500, 390);
    auto* layout = new QVBoxLayout(dialog);
    auto* avatar = new QPushButton;
    avatar->setIcon(avatar_icon(self_));
    avatar->setIconSize(QSize(76, 76));
    avatar->setFixedSize(90, 90);
    auto* nickname = new QLineEdit(user_display_name(self_));
    auto* description = new QLineEdit(text(self_.description()));
    auto* phone = new QLabel(self_.phone().empty() ? QStringLiteral("未绑定") : text(self_.phone()));
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("昵称"), nickname);
    form->addRow(QStringLiteral("签名"), description);
    form->addRow(QStringLiteral("手机号"), phone);
    auto* save = new QPushButton(QStringLiteral("保存昵称和签名"));
    save->setObjectName(QStringLiteral("primaryButton"));
    auto* phone_button = new QPushButton(QStringLiteral("修改绑定手机"));
    layout->addWidget(avatar, 0, Qt::AlignHCenter);
    layout->addLayout(form);
    layout->addWidget(save);
    layout->addWidget(phone_button);
    layout->addStretch();
    connect(avatar, &QPushButton::clicked, this, &AppWindow::update_avatar);
    connect(phone_button, &QPushButton::clicked, this, &AppWindow::update_phone);
    QPointer<QDialog> guarded_dialog(dialog);
    QPointer<QLineEdit> guarded_description(description);
    connect(save, &QPushButton::clicked, dialog, [this, guarded_dialog, guarded_description, nickname] {
        ahwei_im::SetUserNicknameReq nickname_request;
        nickname_request.set_request_id(utf8(GatewayClient::make_request_id()));
        nickname_request.set_session_id(utf8(session_id_));
        nickname_request.set_nickname(utf8(nickname->text().trimmed()));
        gateway_.post<ahwei_im::SetUserNicknameReq, ahwei_im::SetUserNicknameRsp>(
            QStringLiteral("/service/user/set_nickname"), nickname_request,
            [this, guarded_dialog, guarded_description](const ahwei_im::SetUserNicknameRsp& nickname_response, const QString& nickname_transport_error) {
                if (!guarded_dialog || !guarded_description) {
                    return;
                }
                QString error = response_error(nickname_response, nickname_transport_error);
                if (!error.isEmpty()) {
                    show_error(QStringLiteral("修改昵称失败"), error);
                    return;
                }
                ahwei_im::SetUserDescriptionReq description_request;
                description_request.set_request_id(utf8(GatewayClient::make_request_id()));
                description_request.set_session_id(utf8(session_id_));
                description_request.set_description(utf8(guarded_description->text()));
                gateway_.post<ahwei_im::SetUserDescriptionReq, ahwei_im::SetUserDescriptionRsp>(
                    QStringLiteral("/service/user/set_description"), description_request,
                    [this, guarded_dialog](const ahwei_im::SetUserDescriptionRsp& response, const QString& transport_error) {
                        const QString error = response_error(response, transport_error);
                        if (!error.isEmpty()) {
                            show_error(QStringLiteral("修改签名失败"), error);
                            return;
                        }
                        refresh_self();
                        show_status(QStringLiteral("个人资料已更新"));
                        if (guarded_dialog) {
                            guarded_dialog->accept();
                        }
                    });
            });
    });
    dialog->show();
}

void AppWindow::update_avatar() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择头像"), {}, QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        show_error(QStringLiteral("读取头像失败"), file.errorString());
        return;
    }
    const QByteArray avatar = file.readAll();
    ahwei_im::SetUserAvatarReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_avatar(avatar.constData(), static_cast<std::size_t>(avatar.size()));
    gateway_.post<ahwei_im::SetUserAvatarReq, ahwei_im::SetUserAvatarRsp>(
        QStringLiteral("/service/user/set_avatar"), request,
        [this](const ahwei_im::SetUserAvatarRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("修改头像失败"), error);
                return;
            }
            refresh_self();
            show_status(QStringLiteral("头像已更新"));
        });
}

void AppWindow::update_phone() {
    bool ok = false;
    const QString phone = QInputDialog::getText(
        this, QStringLiteral("修改手机号"), QStringLiteral("新手机号"),
        QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || phone.isEmpty()) {
        return;
    }
    ahwei_im::PhoneVerifyCodeReq code_request;
    code_request.set_request_id(utf8(GatewayClient::make_request_id()));
    code_request.set_phone_number(utf8(phone));
    gateway_.post<ahwei_im::PhoneVerifyCodeReq, ahwei_im::PhoneVerifyCodeRsp>(
        QStringLiteral("/service/user/get_phone_verify_code"), code_request,
        [this, phone](const ahwei_im::PhoneVerifyCodeRsp& code_response, const QString& code_transport_error) {
            const QString error = response_error(code_response, code_transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取验证码失败"), error);
                return;
            }
            QMessageBox::information(this, QStringLiteral("验证码"), QStringLiteral("请输入刚刚获取的四位验证码。"));
            bool accepted = false;
            const QString code = QInputDialog::getText(
                this, QStringLiteral("修改手机号"), QStringLiteral("验证码"),
                QLineEdit::Normal, {}, &accepted).trimmed();
            if (!accepted || code.isEmpty()) {
                return;
            }
            ahwei_im::SetUserPhoneNumberReq request;
            request.set_request_id(utf8(GatewayClient::make_request_id()));
            request.set_session_id(utf8(session_id_));
            request.set_phone_number(utf8(phone));
            request.set_phone_verify_code_id(code_response.verify_code_id());
            request.set_phone_verify_code(utf8(code));
            gateway_.post<ahwei_im::SetUserPhoneNumberReq, ahwei_im::SetUserPhoneNumberRsp>(
                QStringLiteral("/service/user/set_phone"), request,
                [this](const ahwei_im::SetUserPhoneNumberRsp& response, const QString& transport_error) {
                    const QString error = response_error(response, transport_error);
                    if (!error.isEmpty()) {
                        show_error(QStringLiteral("修改手机号失败"), error);
                        return;
                    }
                    refresh_self();
                    show_status(QStringLiteral("手机号已更新"));
                });
        });
}

void AppWindow::show_session_members() {
    if (current_session_id_.empty()) {
        return;
    }
    ahwei_im::GetChatSessionMemberReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_chat_session_id(current_session_id_);
    gateway_.post<ahwei_im::GetChatSessionMemberReq, ahwei_im::GetChatSessionMemberRsp>(
        QStringLiteral("/service/friend/get_chat_session_member"), request,
        [this](const ahwei_im::GetChatSessionMemberRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("获取会话成员失败"), error);
                return;
            }
            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("会话成员"));
            dialog.resize(420, 480);
            auto* layout = new QVBoxLayout(&dialog);
            auto* list = new QListWidget;
            for (const auto& member : response.member_info_list()) {
                list->addItem(new QListWidgetItem(avatar_icon(member), user_display_name(member)));
            }
            layout->addWidget(list);
            dialog.exec();
        });
}

void AppWindow::show_history_menu() {
    if (current_session_id_.empty()) {
        return;
    }
    QMenu menu(this);
    QAction* keyword = menu.addAction(QStringLiteral("按关键词搜索"));
    QAction* time = menu.addAction(QStringLiteral("按时间范围查询"));
    QAction* selected = menu.exec(history_button_->mapToGlobal(QPoint(0, history_button_->height())));
    if (selected == keyword) {
        search_history_by_keyword();
    } else if (selected == time) {
        search_history_by_time();
    }
}

void AppWindow::search_history_by_keyword() {
    bool ok = false;
    const QString keyword = QInputDialog::getText(
        this, QStringLiteral("搜索历史消息"), QStringLiteral("关键词"),
        QLineEdit::Normal, {}, &ok);
    if (!ok) {
        return;
    }
    ahwei_im::MsgSearchReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_chat_session_id(current_session_id_);
    request.set_search_key(utf8(keyword));
    gateway_.post<ahwei_im::MsgSearchReq, ahwei_im::MsgSearchRsp>(
        QStringLiteral("/service/message_storage/search_history"), request,
        [this, keyword](const ahwei_im::MsgSearchRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("搜索历史消息失败"), error);
                return;
            }
            show_message_results(QStringLiteral("搜索：%1").arg(keyword), response.msg_list());
        });
}

void AppWindow::search_history_by_time() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("按时间查询历史消息"));
    auto* layout = new QFormLayout(&dialog);
    auto* begin = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-7));
    auto* end = new QDateTimeEdit(QDateTime::currentDateTime());
    begin->setCalendarPopup(true);
    end->setCalendarPopup(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(QStringLiteral("开始"), begin);
    layout->addRow(QStringLiteral("结束"), end);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    ahwei_im::GetHistoryMsgReq request;
    request.set_request_id(utf8(GatewayClient::make_request_id()));
    request.set_session_id(utf8(session_id_));
    request.set_chat_session_id(current_session_id_);
    request.set_start_time(begin->dateTime().toSecsSinceEpoch());
    request.set_over_time(end->dateTime().toSecsSinceEpoch());
    gateway_.post<ahwei_im::GetHistoryMsgReq, ahwei_im::GetHistoryMsgRsp>(
        QStringLiteral("/service/message_storage/get_history"), request,
        [this](const ahwei_im::GetHistoryMsgRsp& response, const QString& transport_error) {
            const QString error = response_error(response, transport_error);
            if (!error.isEmpty()) {
                show_error(QStringLiteral("查询历史消息失败"), error);
                return;
            }
            show_message_results(QStringLiteral("时间范围查询结果"), response.msg_list());
        });
}

void AppWindow::show_message_results(
    const QString& title,
    const google::protobuf::RepeatedPtrField<ahwei_im::MessageInfo>& messages) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(680, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget;
    for (const auto& message : messages) {
        const QString sender = message.has_sender()
            ? user_display_name(message.sender())
            : QStringLiteral("未知用户");
        list->addItem(QStringLiteral("%1 · %2\n%3")
            .arg(sender,
                 QDateTime::fromSecsSinceEpoch(message.timestamp()).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                 message_summary(message)));
    }
    layout->addWidget(list);
    dialog.exec();
}

void AppWindow::handle_notification(const QByteArray& body) {
    ahwei_im::NotifyMessage notification;
    if (!notification.ParseFromArray(body.constData(), body.size())) {
        show_status(QStringLiteral("收到一条无法识别的通知"));
        return;
    }
    switch (notification.notify_type()) {
        case ahwei_im::FRIEND_ADD_APPLY_NOTIFY:
            show_status(QStringLiteral("收到新的好友申请"));
            refresh_applications();
            break;
        case ahwei_im::FRIEND_ADD_PROCESS_NOTIFY:
            show_status(notification.friend_process_result().agree()
                ? QStringLiteral("好友申请已被同意")
                : QStringLiteral("好友申请被拒绝"));
            refresh_friends();
            refresh_sessions();
            break;
        case ahwei_im::CHAT_SESSION_CREATE_NOTIFY:
            show_status(QStringLiteral("会话列表已更新"));
            refresh_sessions();
            break;
        case ahwei_im::CHAT_MESSAGE_NOTIFY: {
            const auto& message = notification.new_message_info().message_info();
            auto& list = messages_[text(message.chat_session_id())];
            const bool duplicate = std::any_of(
                list.begin(), list.end(), [&message](const auto& current) {
                    return current.message_id() == message.message_id();
                });
            if (!duplicate) {
                list.push_back(message);
            }
            if (message.chat_session_id() == current_session_id_) {
                render_messages();
            } else {
                unread_[text(message.chat_session_id())] += 1;
                if (list_mode_ == ListMode::SESSIONS) {
                    render_sessions();
                }
            }
            show_status(QStringLiteral("收到新消息"));
            break;
        }
        case ahwei_im::FRIEND_REMOVE_NOTIFY:
            show_status(QStringLiteral("一位好友删除了与你的好友关系"));
            refresh_friends();
            refresh_sessions();
            break;
        default:
            show_status(QStringLiteral("收到未知通知类型"));
            break;
    }
}

void AppWindow::show_error(const QString& action, const QString& detail) {
    show_status(action + QStringLiteral("：") + detail, 7000);
    QMessageBox::warning(this, action, detail);
}

void AppWindow::show_status(const QString& text_value, int timeout_ms) {
    status_label_->setText(text_value);
    QTimer::singleShot(timeout_ms, this, [this, text_value] {
        if (status_label_->text() == text_value) {
            status_label_->setText(QStringLiteral("就绪"));
        }
    });
}

QString AppWindow::message_summary(const ahwei_im::MessageInfo& message) {
    if (!message.has_message()) {
        return QStringLiteral("[空消息]");
    }
    const auto& content = message.message();
    switch (content.message_type()) {
        case ahwei_im::STRING:
            return content.has_string_message()
                ? text(content.string_message().content())
                : QStringLiteral("[文本消息格式错误]");
        case ahwei_im::IMAGE:
            return QStringLiteral("[图片] 双击预览");
        case ahwei_im::FILE:
            return content.has_file_message()
                ? QStringLiteral("[文件] %1 · %2（双击保存）")
                      .arg(text(content.file_message().file_name()),
                           file_size_text(content.file_message().file_size()))
                : QStringLiteral("[文件消息格式错误]");
        case ahwei_im::SPEECH:
            return QStringLiteral("[语音] 双击保存原始音频");
        default:
            return QStringLiteral("[未知消息]");
    }
}

QString AppWindow::user_display_name(const ahwei_im::UserInfo& user) {
    return user.nickname().empty() ? text(user.user_id()) : text(user.nickname());
}

QString AppWindow::session_display_name(const ahwei_im::ChatSessionInfo& session) {
    return session.chat_session_name().empty()
        ? text(session.chat_session_id())
        : text(session.chat_session_name());
}

QIcon AppWindow::avatar_icon(const ahwei_im::UserInfo& user) {
    QPixmap avatar;
    if (!user.avatar().empty()) {
        avatar.loadFromData(bytes(user.avatar()));
    }
    if (avatar.isNull()) {
        return QIcon(QStringLiteral(":/chat/default-avatar.png"));
    }
    return QIcon(avatar.scaled(80, 80, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

QIcon AppWindow::group_avatar_icon(
    const google::protobuf::RepeatedPtrField<ahwei_im::UserInfo>& members) {
    const int count = std::min(9, members.size());
    if (count == 0) {
        return QIcon(QStringLiteral(":/chat/group-avatar.png"));
    }

    constexpr int edge = 96;
    constexpr int margin = 6;
    constexpr int gap = 4;
    const int columns = count == 1 ? 1 : (count <= 4 ? 2 : 3);
    const int rows = (count + columns - 1) / columns;
    const int tile = (edge - margin * 2 - gap * (columns - 1)) / columns;
    const int content_height = rows * tile + (rows - 1) * gap;
    const int start_y = (edge - content_height) / 2;

    QPixmap canvas(edge, edge);
    canvas.fill(QColor(QStringLiteral("#d8d8d8")));
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    int member_index = 0;
    for (int row = 0; row < rows; ++row) {
        const int items_in_row = row == 0
            ? count - columns * (rows - 1)
            : columns;
        const int row_width = items_in_row * tile + (items_in_row - 1) * gap;
        const int start_x = (edge - row_width) / 2;
        for (int column = 0; column < items_in_row; ++column, ++member_index) {
            QPixmap avatar;
            if (!members.Get(member_index).avatar().empty()) {
                avatar.loadFromData(bytes(members.Get(member_index).avatar()));
            }
            if (avatar.isNull()) {
                avatar = QIcon(QStringLiteral(":/chat/default-avatar.png")).pixmap(tile, tile);
            }
            const QPixmap scaled = avatar.scaled(
                tile, tile, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const QRect source(
                std::max(0, (scaled.width() - tile) / 2),
                std::max(0, (scaled.height() - tile) / 2),
                std::min(tile, scaled.width()),
                std::min(tile, scaled.height()));
            const QRect target(
                start_x + column * (tile + gap),
                start_y + row * (tile + gap),
                tile,
                tile);
            painter.drawPixmap(target, scaled, source);
        }
    }
    painter.end();
    return QIcon(canvas);
}

}  // namespace chat::desktop
