#pragma once

#include "base.pb.h"
#include "chat_client/gateway_client.hpp"
#include "friend.pb.h"

#include <QHash>
#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;

namespace chat::desktop {

class AppWindow final : public QWidget {
    Q_OBJECT

public:
    explicit AppWindow(
        const QUrl& initial_http_url,
        const QUrl& initial_websocket_url,
        QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class ListMode {
        SESSIONS,
        FRIENDS,
        APPLICATIONS,
    };

    struct PendingApplication {
        std::string event_id;
        ahwei_im::UserInfo sender;
    };

    void build_login_page();
    void build_chat_page();
    QWidget* build_account_login_tab();
    QWidget* build_phone_login_tab();
    void apply_style();
    void wire_network_events();

    bool configure_endpoints();
    void set_login_busy(bool busy);
    void account_login();
    void account_register();
    void request_phone_code();
    void phone_login(bool registration);
    void enter_chat(const QString& session_id);
    void logout(bool show_message = false);

    void switch_list_mode(ListMode mode);
    void refresh_visible_list();
    void refresh_self();
    void refresh_friends();
    void refresh_sessions();
    void refresh_applications();
    void render_sessions();
    void render_friends();
    void render_applications();
    void filter_visible_list(const QString& keyword);

    void handle_list_item(QListWidgetItem* item);
    void show_list_context_menu(const QPoint& position);
    void select_session(const std::string& chat_session_id);
    void refresh_recent_messages(const std::string& chat_session_id);
    void render_messages();
    void append_message_item(const ahwei_im::MessageInfo& message);
    void open_message_attachment(QListWidgetItem* item);

    void send_text_message();
    void send_image_message();
    void send_file_message();
    void send_content(const ahwei_im::MessageContent& content);

    void show_add_friend_dialog();
    void process_application(const PendingApplication& application);
    void remove_friend(const std::string& user_id);
    void show_create_group_dialog();
    void show_profile_dialog();
    void update_avatar();
    void update_phone();
    void show_session_members();
    void show_history_menu();
    void search_history_by_keyword();
    void search_history_by_time();
    void show_message_results(
        const QString& title,
        const google::protobuf::RepeatedPtrField<ahwei_im::MessageInfo>& messages);

    void handle_notification(const QByteArray& body);
    void show_error(const QString& action, const QString& detail);
    void show_status(const QString& text, int timeout_ms = 4000);
    static QString message_summary(const ahwei_im::MessageInfo& message);
    static QString user_display_name(const ahwei_im::UserInfo& user);
    static QString session_display_name(const ahwei_im::ChatSessionInfo& session);
    static QIcon avatar_icon(const ahwei_im::UserInfo& user);

    GatewayClient gateway_;
    QString session_id_;
    bool intentional_logout_ = false;
    ListMode list_mode_ = ListMode::SESSIONS;
    std::string current_session_id_;
    std::string phone_code_id_;

    ahwei_im::UserInfo self_;
    std::vector<ahwei_im::UserInfo> friends_;
    std::vector<ahwei_im::ChatSessionInfo> sessions_;
    std::vector<PendingApplication> applications_;
    QHash<QString, std::vector<ahwei_im::MessageInfo>> messages_;
    QHash<QString, int> unread_;

    QStackedWidget* pages_ = nullptr;
    QWidget* login_page_ = nullptr;
    QWidget* chat_page_ = nullptr;
    QLineEdit* http_url_edit_ = nullptr;
    QLineEdit* websocket_url_edit_ = nullptr;
    QLineEdit* username_edit_ = nullptr;
    QLineEdit* password_edit_ = nullptr;
    QLineEdit* phone_edit_ = nullptr;
    QLineEdit* phone_code_edit_ = nullptr;
    QPushButton* account_login_button_ = nullptr;
    QPushButton* account_register_button_ = nullptr;
    QPushButton* phone_code_button_ = nullptr;
    QPushButton* phone_login_button_ = nullptr;
    QPushButton* phone_register_button_ = nullptr;
    QLabel* login_status_ = nullptr;

    QPushButton* profile_button_ = nullptr;
    QPushButton* sessions_button_ = nullptr;
    QPushButton* friends_button_ = nullptr;
    QPushButton* applications_button_ = nullptr;
    QLabel* connection_label_ = nullptr;
    QLineEdit* filter_edit_ = nullptr;
    QPushButton* list_action_button_ = nullptr;
    QPushButton* refresh_button_ = nullptr;
    QListWidget* entity_list_ = nullptr;
    QLabel* conversation_title_ = nullptr;
    QPushButton* members_button_ = nullptr;
    QPushButton* history_button_ = nullptr;
    QListWidget* message_list_ = nullptr;
    QPlainTextEdit* composer_ = nullptr;
    QPushButton* send_button_ = nullptr;
    QPushButton* image_button_ = nullptr;
    QPushButton* file_button_ = nullptr;
    QPushButton* speech_button_ = nullptr;
    QLabel* status_label_ = nullptr;
};

}  // namespace chat::desktop
