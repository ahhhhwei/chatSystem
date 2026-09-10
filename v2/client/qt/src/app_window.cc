#include "chat_client/app_window.hpp"

#include "file.pb.h"
#include "gateway.pb.h"
#include "message.pb.h"
#include "notify.pb.h"
#include "transmit.pb.h"
#include "user.pb.h"

#include <QBoxLayout>
#include <QCloseEvent>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
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

}  // namespace

AppWindow::AppWindow(
    const QUrl& initial_http_url,
    const QUrl& initial_websocket_url,
    QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("Ahwei Chat V2"));
    setWindowIcon(QIcon(QStringLiteral(":/chat/logo.png")));
    // 当前服务器桌面为 1024x768；这个尺寸也能在常见远程桌面中完整展示。
    resize(1000, 700);
    setMinimumSize(900, 620);

    pages_ = new QStackedWidget(this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(pages_);
    build_login_page();
    build_chat_page();
    apply_style();
    wire_network_events();

    QSettings settings;
    http_url_edit_->setText(settings.value(
        QStringLiteral("gateway/http"), initial_http_url.toString()).toString());
    websocket_url_edit_->setText(settings.value(
        QStringLiteral("gateway/websocket"),
        initial_websocket_url.toString()).toString());
    pages_->setCurrentWidget(login_page_);
}

void AppWindow::closeEvent(QCloseEvent* event) {
    intentional_logout_ = true;
    gateway_.close_websocket();
    QWidget::closeEvent(event);
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

    auto* brand = new QLabel(QStringLiteral("Ahwei Chat"));
    brand->setObjectName(QStringLiteral("brandTitle"));
    brand->setAlignment(Qt::AlignCenter);
    auto* subtitle = new QLabel(QStringLiteral("V2 · Protobuf + HTTP/WebSocket"));
    subtitle->setObjectName(QStringLiteral("subtleLabel"));
    subtitle->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(brand);
    card_layout->addWidget(subtitle);

    auto* tabs = new QTabWidget;
    tabs->addTab(build_account_login_tab(), QStringLiteral("账号登录"));
    tabs->addTab(build_phone_login_tab(), QStringLiteral("手机登录"));
    card_layout->addWidget(tabs);

    auto* endpoints = new QGroupBox(QStringLiteral("Gateway 地址"));
    auto* endpoint_form = new QFormLayout(endpoints);
    http_url_edit_ = new QLineEdit;
    websocket_url_edit_ = new QLineEdit;
    http_url_edit_->setPlaceholderText(QStringLiteral("http://127.0.0.1:9000"));
    websocket_url_edit_->setPlaceholderText(QStringLiteral("ws://127.0.0.1:9001"));
    endpoint_form->addRow(QStringLiteral("HTTP"), http_url_edit_);
    endpoint_form->addRow(QStringLiteral("WebSocket"), websocket_url_edit_);
    card_layout->addWidget(endpoints);

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
    username_edit_->setPlaceholderText(QStringLiteral("用户名（少于 22 个 UTF-8 字节）"));
    password_edit_->setPlaceholderText(QStringLiteral("密码（6–15 位字母、数字、_、-）"));
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
    phone_code_edit_->setPlaceholderText(QStringLiteral("短信验证码（开发环境查看 UserServer 日志）"));
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
    navigation->setFixedWidth(92);
    auto* navigation_layout = new QVBoxLayout(navigation);
    navigation_layout->setContentsMargins(12, 24, 12, 18);
    navigation_layout->setSpacing(12);
    profile_button_ = new QPushButton;
    profile_button_->setObjectName(QStringLiteral("avatarButton"));
    profile_button_->setIcon(QIcon(QStringLiteral(":/chat/default-avatar.png")));
    profile_button_->setIconSize(QSize(46, 46));
    profile_button_->setFixedSize(58, 58);
    sessions_button_ = new QPushButton(QStringLiteral("消息"));
    friends_button_ = new QPushButton(QStringLiteral("好友"));
    applications_button_ = new QPushButton(QStringLiteral("申请"));
    for (auto* button : {sessions_button_, friends_button_, applications_button_}) {
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("navButton"));
        button->setFixedHeight(48);
    }
    connection_label_ = new QLabel(QStringLiteral("未连接"));
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
    entities->setMinimumWidth(290);
    entities->setMaximumWidth(370);
    auto* entity_layout = new QVBoxLayout(entities);
    entity_layout->setContentsMargins(14, 18, 14, 14);
    entity_layout->setSpacing(10);
    auto* entity_header = new QHBoxLayout;
    filter_edit_ = new QLineEdit;
    filter_edit_->setPlaceholderText(QStringLiteral("筛选当前列表"));
    list_action_button_ = new QPushButton(QStringLiteral("＋"));
    list_action_button_->setToolTip(QStringLiteral("添加好友或创建群聊"));
    refresh_button_ = new QPushButton(QStringLiteral("↻"));
    refresh_button_->setToolTip(QStringLiteral("刷新"));
    list_action_button_->setFixedSize(38, 38);
    refresh_button_->setFixedSize(38, 38);
    entity_header->addWidget(filter_edit_, 1);
    entity_header->addWidget(list_action_button_);
    entity_header->addWidget(refresh_button_);
    entity_list_ = new QListWidget;
    entity_list_->setObjectName(QStringLiteral("entityList"));
    entity_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    entity_layout->addLayout(entity_header);
    entity_layout->addWidget(entity_list_, 1);

    auto* conversation = new QFrame;
    conversation->setObjectName(QStringLiteral("conversationPanel"));
    auto* conversation_layout = new QVBoxLayout(conversation);
    conversation_layout->setContentsMargins(0, 0, 0, 0);
    conversation_layout->setSpacing(0);
    auto* title_bar = new QFrame;
    title_bar->setObjectName(QStringLiteral("titleBar"));
    title_bar->setFixedHeight(68);
    auto* title_layout = new QHBoxLayout(title_bar);
    title_layout->setContentsMargins(22, 0, 18, 0);
    conversation_title_ = new QLabel(QStringLiteral("请选择一个会话"));
    conversation_title_->setObjectName(QStringLiteral("conversationTitle"));
    members_button_ = new QPushButton(QStringLiteral("成员"));
    history_button_ = new QPushButton(QStringLiteral("历史"));
    members_button_->setEnabled(false);
    history_button_->setEnabled(false);
    title_layout->addWidget(conversation_title_, 1);
    title_layout->addWidget(members_button_);
    title_layout->addWidget(history_button_);
    message_list_ = new QListWidget;
    message_list_->setObjectName(QStringLiteral("messageList"));
    message_list_->setSpacing(7);
    auto* composer_panel = new QFrame;
    composer_panel->setObjectName(QStringLiteral("composerPanel"));
    composer_panel->setFixedHeight(190);
    auto* composer_layout = new QVBoxLayout(composer_panel);
    composer_layout->setContentsMargins(18, 12, 18, 14);
    auto* tools = new QHBoxLayout;
    image_button_ = new QPushButton(QStringLiteral("图片"));
    file_button_ = new QPushButton(QStringLiteral("文件"));
    speech_button_ = new QPushButton(QStringLiteral("语音"));
    speech_button_->setEnabled(false);
    speech_button_->setToolTip(QStringLiteral("SpeechServer 尚未启用，最后阶段接入"));
    tools->addWidget(image_button_);
    tools->addWidget(file_button_);
    tools->addWidget(speech_button_);
    tools->addStretch();
    composer_ = new QPlainTextEdit;
    composer_->setPlaceholderText(QStringLiteral("输入消息，Ctrl+Enter 发送"));
    send_button_ = new QPushButton(QStringLiteral("发送"));
    send_button_->setObjectName(QStringLiteral("primaryButton"));
    send_button_->setFixedWidth(100);
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
    setStyleSheet(QStringLiteral(R"(
        QWidget { font-family: "Noto Sans CJK SC", "Microsoft YaHei", sans-serif; color: #172033; }
        #loginCard { background: white; border: 1px solid #dfe5ec; border-radius: 18px; }
        #brandTitle { font-size: 30px; font-weight: 700; color: #172033; }
        #subtleLabel { color: #768196; }
        QLineEdit, QPlainTextEdit { background: white; border: 1px solid #d8dee8; border-radius: 8px; padding: 9px; selection-background-color: #5b7cff; }
        QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid #5b7cff; }
        QPushButton { background: #eef1f6; border: none; border-radius: 8px; padding: 9px 14px; }
        QPushButton:hover { background: #e0e6f0; }
        QPushButton:disabled { color: #a6adba; background: #f0f1f3; }
        #primaryButton { color: white; background: #526ff5; font-weight: 600; }
        #primaryButton:hover { background: #3f5ee5; }
        #navigation { background: #20283a; }
        #avatarButton { background: transparent; padding: 2px; }
        #navButton { color: #c9d1df; background: transparent; padding: 8px; }
        #navButton:hover, #navButton:checked { color: white; background: #35415b; }
        #connectionLabel { color: #9aa8c0; font-size: 11px; }
        #entityPanel { background: #f6f8fb; border-right: 1px solid #dfe4ec; }
        #entityList, #messageList { border: none; background: transparent; outline: none; }
        #entityList::item { padding: 12px 9px; border-radius: 8px; margin: 2px; }
        #entityList::item:selected { color: #172033; background: #dfe6ff; }
        #conversationPanel { background: #f4f6f9; }
        #titleBar { background: white; border-bottom: 1px solid #e2e7ee; }
        #conversationTitle { font-size: 19px; font-weight: 650; }
        #messageList::item { background: white; border: 1px solid #e3e7ed; border-radius: 10px; padding: 10px; margin-left: 18px; margin-right: 18px; }
        #composerPanel { background: white; border-top: 1px solid #e2e7ee; }
        #statusBar { color: #6d7789; background: white; padding-left: 12px; border-top: 1px solid #edf0f4; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { padding: 9px 18px; }
        QTabBar::tab:selected { color: #526ff5; border-bottom: 2px solid #526ff5; }
    )"));
}

void AppWindow::wire_network_events() {
    connect(&gateway_, &GatewayClient::websocketConnected, this, [this] {
        connection_label_->setText(QStringLiteral("实时连接\n已建立"));
        show_status(QStringLiteral("WebSocket 已连接并发送会话鉴权"));
    });
    connect(&gateway_, &GatewayClient::websocketError, this, [this](const QString& error) {
        connection_label_->setText(QStringLiteral("实时连接\n异常"));
        show_status(QStringLiteral("WebSocket：") + error, 6000);
    });
    connect(&gateway_, &GatewayClient::websocketDisconnected, this, [this] {
        connection_label_->setText(QStringLiteral("实时连接\n已断开"));
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
        QMessageBox::warning(this, QStringLiteral("地址错误"), QStringLiteral("请输入有效的 HTTP 和 WebSocket Gateway 地址。"));
        return false;
    }
    gateway_.configure(http, websocket);
    QSettings settings;
    settings.setValue(QStringLiteral("gateway/http"), http.toString());
    settings.setValue(QStringLiteral("gateway/websocket"), websocket.toString());
    return true;
}

void AppWindow::set_login_busy(bool busy) {
    for (auto* button : {
             account_login_button_, account_register_button_, phone_code_button_,
             phone_login_button_, phone_register_button_}) {
        button->setEnabled(!busy);
    }
    login_status_->setText(busy ? QStringLiteral("正在连接 Gateway…") : QStringLiteral("请输入账号，或先注册新用户"));
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
                QStringLiteral("开发阶段验证码已输出到 UserServer 日志，有效期 5 分钟且只能使用一次。"));
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
    pages_->setCurrentWidget(chat_page_);
    connection_label_->setText(QStringLiteral("实时连接\n连接中"));
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
        ? QStringLiteral("实时连接已断开，会话已注销，请重新登录")
        : QStringLiteral("已退出登录"));
    if (show_message) {
        QMessageBox::warning(this, QStringLiteral("连接断开"), QStringLiteral("Gateway 已断开实时连接。按照 v1 规则，该登录会话同时失效。"));
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
        });
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
        QString title = session_display_name(session);
        if (unread > 0) {
            title += QStringLiteral("  (%1 条新消息)").arg(unread);
        }
        auto* item = new QListWidgetItem(
            session.single_chat_friend_id().empty()
                ? QIcon(QStringLiteral(":/chat/group-avatar.png"))
                : QIcon(QStringLiteral(":/chat/default-avatar.png")),
            title + QStringLiteral("\n") + summary);
        item->setData(kEntityIdRole, id);
        item->setToolTip(id);
        entity_list_->addItem(item);
    }
    filter_visible_list(filter_edit_->text());
}

void AppWindow::render_friends() {
    entity_list_->clear();
    for (const auto& friend_info : friends_) {
        auto* item = new QListWidgetItem(
            avatar_icon(friend_info),
            user_display_name(friend_info) + QStringLiteral("\n") +
                (friend_info.description().empty()
                     ? QStringLiteral("暂无签名")
                     : text(friend_info.description())));
        item->setData(kEntityIdRole, text(friend_info.user_id()));
        item->setToolTip(QStringLiteral("双击进入会话，右键管理好友"));
        entity_list_->addItem(item);
    }
    filter_visible_list(filter_edit_->text());
}

void AppWindow::render_applications() {
    entity_list_->clear();
    for (const auto& application : applications_) {
        auto* item = new QListWidgetItem(
            avatar_icon(application.sender),
            user_display_name(application.sender) + QStringLiteral("\n请求添加你为好友"));
        item->setData(kEntityIdRole, text(application.event_id));
        item->setData(kEntityAuxRole, text(application.sender.user_id()));
        item->setToolTip(QStringLiteral("双击处理好友申请"));
        entity_list_->addItem(item);
    }
    filter_visible_list(filter_edit_->text());
}

void AppWindow::filter_visible_list(const QString& keyword) {
    for (int index = 0; index < entity_list_->count(); ++index) {
        auto* item = entity_list_->item(index);
        item->setHidden(!item->text().contains(keyword, Qt::CaseInsensitive));
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
    auto* item = new QListWidgetItem(
        QStringLiteral("%1  ·  %2\n%3").arg(sender, timestamp, message_summary(message)));
    if (message.has_message()) {
        if (message.message().message_type() == ahwei_im::IMAGE) {
            QPixmap image;
            image.loadFromData(bytes(message.message().image_message().image_content()));
            item->setIcon(image.isNull()
                ? QIcon(QStringLiteral(":/chat/image.png"))
                : QIcon(image.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        } else if (message.message().message_type() == ahwei_im::FILE) {
            item->setIcon(QIcon(QStringLiteral(":/chat/file.png")));
        }
    }
    const std::string serialized = message.SerializeAsString();
    item->setData(kSerializedMessageRole, QByteArray(
        serialized.data(), static_cast<int>(serialized.size())));
    if (message.has_sender() && message.sender().user_id() == self_.user_id()) {
        item->setTextAlignment(Qt::AlignRight);
    }
    message_list_->addItem(item);
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
        show_error(QStringLiteral("下载附件失败"), QStringLiteral("消息中没有 file_id"));
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
            show_status(QStringLiteral("消息已发送并持久化"));
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
                show_status(QStringLiteral("群聊已创建，等待实时会话通知"));
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
            QMessageBox::information(this, QStringLiteral("验证码"), QStringLiteral("请从 UserServer 日志读取四位开发验证码。"));
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
        show_status(QStringLiteral("收到无法解析的 WebSocket 通知"));
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
            show_status(QStringLiteral("收到新会话通知"));
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

}  // namespace chat::desktop
