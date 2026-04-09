#include <gtk/gtk.h>

#include "secure_file_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    COLUMN_NAME = 0,
    COLUMN_SIZE,
    COLUMN_MODIFIED,
    COLUMN_COUNT
};

typedef struct secure_file_gui_state {
    GtkWidget *window;
    GtkWidget *storage_entry;
    GtkWidget *file_name_entry;
    GtkWidget *key_entry;
    GtkWidget *tree_view;
    GtkListStore *list_store;
    GtkWidget *editor_view;
    GtkTextBuffer *editor_buffer;
    GtkWidget *status_label;
    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;
    GtkWidget *open_button;
    GtkWidget *delete_button;
    GtkWidget *save_button;
    char storage_directory[SECURE_STORAGE_PATH_MAX];
} secure_file_gui_state;

static void gui_load_css(void)
{
    GtkCssProvider *provider;
    const char *css =
        "entry.mono, textview.mono { font-family: Monospace; }"
        "#status-label { padding: 6px; }";

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void gui_append_log(secure_file_gui_state *state, const char *format, ...)
{
    GtkTextIter end_iter;
    GtkTextMark *mark;
    char message[1024];
    char line[1200];
    char timestamp[64];
    time_t now;
    struct tm local_tm;
    va_list args;

    if (!state || !state->log_buffer || !state->log_view)
        return;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    now = time(NULL);
    localtime_r(&now, &local_tm);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_tm);
    snprintf(line, sizeof(line), "[%s] %s\n", timestamp, message);

    gtk_text_buffer_get_end_iter(state->log_buffer, &end_iter);
    gtk_text_buffer_insert(state->log_buffer, &end_iter, line, -1);
    gtk_text_buffer_get_end_iter(state->log_buffer, &end_iter);
    mark = gtk_text_buffer_create_mark(state->log_buffer, NULL, &end_iter, FALSE);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(state->log_view), mark);
    gtk_text_buffer_delete_mark(state->log_buffer, mark);
}

static void gui_set_status(secure_file_gui_state *state,
                           const char *text,
                           gboolean is_error)
{
    char *escaped;
    char *markup;

    escaped = g_markup_escape_text(text ? text : "", -1);
    markup = g_strdup_printf("<span foreground='%s' weight='bold'>%s</span>",
                             is_error ? "#b71c1c" : "#1b5e20",
                             escaped);
    gtk_label_set_markup(GTK_LABEL(state->status_label), markup);
    g_free(markup);
    g_free(escaped);
}

static void gui_set_editor_text(secure_file_gui_state *state,
                                const char *text,
                                gssize length)
{
    gtk_text_buffer_set_text(state->editor_buffer, text ? text : "", length);
}

static gboolean gui_get_selected_name(secure_file_gui_state *state, gchar **name_out)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!state || !name_out)
        return FALSE;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return FALSE;

    gtk_tree_model_get(model, &iter, COLUMN_NAME, name_out, -1);
    return (*name_out != NULL);
}

static void gui_update_action_buttons(secure_file_gui_state *state)
{
    GtkTreeSelection *selection;
    gboolean has_selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    has_selection = gtk_tree_selection_count_selected_rows(selection) > 0;

    gtk_widget_set_sensitive(state->open_button, has_selection);
    gtk_widget_set_sensitive(state->delete_button, has_selection);
}

static void gui_select_file_by_name(secure_file_gui_state *state, const char *file_name)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean valid;

    if (!state || !file_name || file_name[0] == '\0')
        return;

    model = GTK_TREE_MODEL(state->list_store);
    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gchar *listed_name = NULL;

        gtk_tree_model_get(model, &iter, COLUMN_NAME, &listed_name, -1);
        if (listed_name && strcmp(listed_name, file_name) == 0) {
            GtkTreeSelection *selection =
                gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
            gtk_tree_selection_select_iter(selection, &iter);
            g_free(listed_name);
            return;
        }

        g_free(listed_name);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
}

static void gui_refresh_file_list(secure_file_gui_state *state, gboolean log_refresh)
{
    struct secure_storage_entry *entries = NULL;
    size_t entry_count = 0;
    char error_message[SECURE_FILE_ERROR_MAX];
    size_t index;
    int ret;

    memset(error_message, 0, sizeof(error_message));
    ret = secure_storage_list_files(state->storage_directory,
                                    &entries,
                                    &entry_count,
                                    error_message,
                                    sizeof(error_message));
    if (ret != 0) {
        gui_set_status(state,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                       TRUE);
        gui_append_log(state, "%s",
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        return;
    }

    gtk_list_store_clear(state->list_store);
    for (index = 0; index < entry_count; ++index) {
        GtkTreeIter iter;
        gchar *size_text;
        char modified_text[64];
        struct tm local_tm;

        if (entries[index].modified_time > 0) {
            localtime_r(&entries[index].modified_time, &local_tm);
            strftime(modified_text, sizeof(modified_text), "%Y-%m-%d %H:%M", &local_tm);
        } else {
            snprintf(modified_text, sizeof(modified_text), "-");
        }

        size_text = g_format_size((goffset)entries[index].encrypted_size);
        gtk_list_store_append(state->list_store, &iter);
        gtk_list_store_set(state->list_store, &iter,
                           COLUMN_NAME, entries[index].name,
                           COLUMN_SIZE, size_text,
                           COLUMN_MODIFIED, modified_text,
                           -1);
        g_free(size_text);
    }

    gui_update_action_buttons(state);
    gui_set_status(state, "Đã làm mới kho lưu trữ bảo mật", FALSE);
    if (log_refresh) {
        gui_append_log(state,
                       "Đã tải %zu tệp bảo mật từ %s",
                       entry_count,
                       state->storage_directory);
    }

    secure_storage_free_entries(entries);
}

static void gui_open_selected_file(secure_file_gui_state *state)
{
    gchar *selected_name = NULL;
    unsigned char *plain_data = NULL;
    size_t plain_len = 0;
    char error_message[SECURE_FILE_ERROR_MAX];
    const char *key_hex;
    int ret;

    if (!gui_get_selected_name(state, &selected_name)) {
        gui_set_status(state, "Hãy chọn một tệp bảo mật trước", TRUE);
        return;
    }

    key_hex = gtk_entry_get_text(GTK_ENTRY(state->key_entry));
    if (!key_hex || key_hex[0] == '\0') {
        gui_set_status(state, "Hãy nhập khóa AES trước khi mở tệp", TRUE);
        g_free(selected_name);
        return;
    }

    memset(error_message, 0, sizeof(error_message));
    ret = secure_storage_read_file(state->storage_directory,
                                   SECURE_AES_DEVICE_NAME,
                                   selected_name,
                                   key_hex,
                                   &plain_data,
                                   &plain_len,
                                   error_message,
                                   sizeof(error_message));
    if (ret != 0) {
        gui_set_status(state,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                       TRUE);
        gui_append_log(state, "Mở %s thất bại: %s",
                       selected_name,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        g_free(selected_name);
        return;
    }

    if (plain_len != 0 &&
        !g_utf8_validate((const gchar *)plain_data, (gssize)plain_len, NULL)) {
        gui_set_status(state,
                       "Tệp sau khi giải mã không phải văn bản UTF-8 hợp lệ. Hãy dùng CLI cho dữ liệu nhị phân.",
                       TRUE);
        gui_append_log(state,
                       "Mở %s thất bại: nội dung giải mã không phải văn bản UTF-8 hợp lệ",
                       selected_name);
        secure_storage_free_buffer(plain_data, plain_len);
        g_free(selected_name);
        return;
    }

    gtk_entry_set_text(GTK_ENTRY(state->file_name_entry), selected_name);
    gui_set_editor_text(state, (const char *)plain_data, (gssize)plain_len);
    gui_set_status(state, "Đã mở và giải mã tệp bảo mật", FALSE);
    gui_append_log(state, "Đã mở %s (%zu byte bản rõ)",
                   selected_name, plain_len);

    secure_storage_free_buffer(plain_data, plain_len);
    g_free(selected_name);
}

static void on_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    GtkTreeModel *model;
    GtkTreeIter iter;

    (void)selection;
    gui_update_action_buttons(state);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *selected_name = NULL;

        gtk_tree_model_get(model, &iter, COLUMN_NAME, &selected_name, -1);
        if (selected_name) {
            gtk_entry_set_text(GTK_ENTRY(state->file_name_entry), selected_name);
            g_free(selected_name);
        }
    }
}

static void on_row_activated(GtkTreeView *tree_view,
                             GtkTreePath *path,
                             GtkTreeViewColumn *column,
                             gpointer user_data)
{
    (void)tree_view;
    (void)path;
    (void)column;
    gui_open_selected_file((secure_file_gui_state *)user_data);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    gui_refresh_file_list((secure_file_gui_state *)user_data, TRUE);
}

static void on_new_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    GtkTreeSelection *selection;

    (void)button;
    gtk_entry_set_text(GTK_ENTRY(state->file_name_entry), "");
    gui_set_editor_text(state, "", 0);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    gtk_tree_selection_unselect_all(selection);
    gui_update_action_buttons(state);
    gui_set_status(state, "Sẵn sàng tạo tệp bảo mật mới", FALSE);
}

static void on_open_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    gui_open_selected_file((secure_file_gui_state *)user_data);
}

static void on_delete_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    gchar *selected_name = NULL;
    GtkWidget *dialog;

    (void)button;
    if (!gui_get_selected_name(state, &selected_name)) {
        gui_set_status(state, "Hãy chọn một tệp bảo mật để xóa", TRUE);
        return;
    }

    dialog = gtk_message_dialog_new(GTK_WINDOW(state->window),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Xóa tệp bảo mật '%s'?",
                                    selected_name);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "Thao tác này sẽ xóa tệp đã mã hóa khỏi kho lưu trữ riêng.");

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        char error_message[SECURE_FILE_ERROR_MAX];
        int ret;

        memset(error_message, 0, sizeof(error_message));
        ret = secure_storage_delete_file(state->storage_directory,
                                         selected_name,
                                         error_message,
                                         sizeof(error_message));
        if (ret != 0) {
            gui_set_status(state,
                           error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                           TRUE);
            gui_append_log(state, "Xóa %s thất bại: %s",
                           selected_name,
                           error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        } else {
            if (strcmp(gtk_entry_get_text(GTK_ENTRY(state->file_name_entry)), selected_name) == 0) {
                gtk_entry_set_text(GTK_ENTRY(state->file_name_entry), "");
                gui_set_editor_text(state, "", 0);
            }

            gui_set_status(state, "Đã xóa tệp bảo mật", FALSE);
            gui_append_log(state, "Đã xóa %s khỏi kho lưu trữ bảo mật", selected_name);
            gui_refresh_file_list(state, FALSE);
        }
    }

    gtk_widget_destroy(dialog);
    g_free(selected_name);
}

static void on_save_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    GtkTextIter start_iter;
    GtkTextIter end_iter;
    gchar *plain_text;
    const char *file_name;
    const char *key_hex;
    char error_message[SECURE_FILE_ERROR_MAX];
    size_t encrypted_size = 0;
    int ret;

    (void)button;
    file_name = gtk_entry_get_text(GTK_ENTRY(state->file_name_entry));
    key_hex = gtk_entry_get_text(GTK_ENTRY(state->key_entry));

    if (!file_name || file_name[0] == '\0') {
        gui_set_status(state, "Hãy nhập tên tệp trước khi lưu", TRUE);
        return;
    }

    if (!key_hex || key_hex[0] == '\0') {
        gui_set_status(state, "Hãy nhập khóa AES trước khi lưu", TRUE);
        return;
    }

    gtk_text_buffer_get_start_iter(state->editor_buffer, &start_iter);
    gtk_text_buffer_get_end_iter(state->editor_buffer, &end_iter);
    plain_text = gtk_text_buffer_get_text(state->editor_buffer,
                                          &start_iter,
                                          &end_iter,
                                          FALSE);

    memset(error_message, 0, sizeof(error_message));
    ret = secure_storage_save_file(state->storage_directory,
                                   SECURE_AES_DEVICE_NAME,
                                   file_name,
                                   key_hex,
                                   (const unsigned char *)plain_text,
                                   strlen(plain_text),
                                   1,
                                   &encrypted_size,
                                   error_message,
                                   sizeof(error_message));
    if (ret != 0) {
        gui_set_status(state,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                       TRUE);
        gui_append_log(state, "Lưu %s thất bại: %s",
                       file_name,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        g_free(plain_text);
        return;
    }

    gui_set_status(state, "Đã lưu tệp bảo mật với dữ liệu được mã hóa", FALSE);
    gui_append_log(state, "Đã lưu %s vào kho lưu trữ bảo mật (%zu byte đã mã hóa)",
                   file_name, encrypted_size);
    gui_refresh_file_list(state, FALSE);
    gui_select_file_by_name(state, file_name);
    g_free(plain_text);
}

static GtkWidget *create_labeled_entry_row(const char *label_text,
                                           GtkWidget **entry_out,
                                           const char *placeholder,
                                           gboolean monospace,
                                           gboolean editable)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *entry = gtk_entry_new();

    gtk_widget_set_hexpand(entry, TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    gtk_editable_set_editable(GTK_EDITABLE(entry), editable);
    if (!editable)
        gtk_widget_set_sensitive(entry, FALSE);
    if (monospace)
        gtk_style_context_add_class(gtk_widget_get_style_context(entry), "mono");

    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 0);

    *entry_out = entry;
    return box;
}

static void build_gui(GtkApplication *application, secure_file_gui_state *state)
{
    GtkWidget *root_box;
    GtkWidget *paned;
    GtkWidget *list_frame;
    GtkWidget *list_box;
    GtkWidget *list_toolbar;
    GtkWidget *refresh_button;
    GtkWidget *new_button;
    GtkWidget *list_scroll;
    GtkWidget *editor_frame;
    GtkWidget *editor_box;
    GtkWidget *form_grid;
    GtkWidget *button_row;
    GtkWidget *note_label;
    GtkWidget *editor_scroll;
    GtkWidget *log_label;
    GtkWidget *log_scroll;
    GtkWidget *header_bar;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    state->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(state->window), "Trình Quản Lý File Có Bảo Mật");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1200, 760);
    gtk_window_set_position(GTK_WINDOW(state->window), GTK_WIN_POS_CENTER);

    header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Trình Quản Lý File Có Bảo Mật");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar),
                                "Kho lưu trữ riêng với AES trong driver kernel");
    gtk_window_set_titlebar(GTK_WINDOW(state->window), header_bar);

    root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(root_box), 12);
    gtk_container_add(GTK_CONTAINER(state->window), root_box);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root_box), paned, TRUE, TRUE, 0);

    list_frame = gtk_frame_new("Tệp Bảo Mật");
    editor_frame = gtk_frame_new("Trình Soạn Thảo");
    gtk_paned_pack1(GTK_PANED(paned), list_frame, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), editor_frame, TRUE, FALSE);

    list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(list_box), 10);
    gtk_container_add(GTK_CONTAINER(list_frame), list_box);

    list_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    refresh_button = gtk_button_new_with_label("Làm Mới");
    new_button = gtk_button_new_with_label("Tệp Mới");
    state->open_button = gtk_button_new_with_label("Mở");
    state->delete_button = gtk_button_new_with_label("Xóa");
    gtk_widget_set_sensitive(state->open_button, FALSE);
    gtk_widget_set_sensitive(state->delete_button, FALSE);
    gtk_box_pack_start(GTK_BOX(list_toolbar), refresh_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(list_toolbar), new_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(list_toolbar), state->delete_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(list_toolbar), state->open_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(list_box), list_toolbar, FALSE, FALSE, 0);

    state->list_store = gtk_list_store_new(COLUMN_COUNT,
                                           G_TYPE_STRING,
                                           G_TYPE_STRING,
                                           G_TYPE_STRING);
    state->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(state->list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(state->tree_view), TRUE);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Tên", renderer, "text", COLUMN_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->tree_view), column);
    column = gtk_tree_view_column_new_with_attributes("Kích Thước Mã Hóa", renderer, "text", COLUMN_SIZE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->tree_view), column);
    column = gtk_tree_view_column_new_with_attributes("Cập Nhật", renderer, "text", COLUMN_MODIFIED, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->tree_view), column);

    list_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(list_scroll), state->tree_view);
    gtk_box_pack_start(GTK_BOX(list_box), list_scroll, TRUE, TRUE, 0);

    editor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(editor_box), 10);
    gtk_container_add(GTK_CONTAINER(editor_frame), editor_box);

    form_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(form_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(form_grid), 8);
    gtk_box_pack_start(GTK_BOX(editor_box), form_grid, FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(form_grid), create_labeled_entry_row("Kho Lưu Trữ",
                                                                  &state->storage_entry,
                                                                  "",
                                                                  TRUE,
                                                                  FALSE),
                    0, 0, 3, 1);

    gtk_grid_attach(GTK_GRID(form_grid), create_labeled_entry_row("Tên Tệp",
                                                                  &state->file_name_entry,
                                                                  "ví dụ: ghichu.txt",
                                                                  TRUE,
                                                                  TRUE),
                    0, 1, 3, 1);

    gtk_grid_attach(GTK_GRID(form_grid), create_labeled_entry_row("Khóa AES",
                                                                  &state->key_entry,
                                                                  "32, 48 hoặc 64 ký tự hex",
                                                                  TRUE,
                                                                  TRUE),
                    0, 2, 3, 1);

    button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->save_button = gtk_button_new_with_label("Lưu Đã Mã Hóa");
    gtk_box_pack_end(GTK_BOX(button_row), state->save_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(editor_box), button_row, FALSE, FALSE, 0);

    note_label = gtk_label_new("Nội dung bản rõ trong trình soạn thảo chỉ tồn tại trong bộ nhớ. Khi bạn lưu, dữ liệu sẽ được mã hóa bằng AES thông qua driver /dev/secure_aes và được lưu trong folder chứa dữ liệu của chương trình.");
    gtk_label_set_line_wrap(GTK_LABEL(note_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(editor_box), note_label, FALSE, FALSE, 0);

    editor_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(editor_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    state->editor_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(state->editor_view), GTK_WRAP_WORD_CHAR);
    gtk_style_context_add_class(gtk_widget_get_style_context(state->editor_view), "mono");
    state->editor_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->editor_view));
    gtk_container_add(GTK_CONTAINER(editor_scroll), state->editor_view);
    gtk_box_pack_start(GTK_BOX(editor_box), editor_scroll, TRUE, TRUE, 0);

    log_label = gtk_label_new("Nhật Ký Thao Tác");
    gtk_label_set_xalign(GTK_LABEL(log_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(editor_box), log_label, FALSE, FALSE, 0);

    log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    state->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(state->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(state->log_view), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(state->log_view), "mono");
    state->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->log_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), state->log_view);
    gtk_box_pack_start(GTK_BOX(editor_box), log_scroll, TRUE, TRUE, 0);

    state->status_label = gtk_label_new("");
    gtk_widget_set_name(state->status_label, "status-label");
    gtk_label_set_xalign(GTK_LABEL(state->status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(editor_box), state->status_label, FALSE, FALSE, 0);

    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), state);
    g_signal_connect(new_button, "clicked", G_CALLBACK(on_new_clicked), state);
    g_signal_connect(state->open_button, "clicked", G_CALLBACK(on_open_clicked), state);
    g_signal_connect(state->delete_button, "clicked", G_CALLBACK(on_delete_clicked), state);
    g_signal_connect(state->save_button, "clicked", G_CALLBACK(on_save_clicked), state);
    g_signal_connect(state->tree_view, "row-activated", G_CALLBACK(on_row_activated), state);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), state);

    gtk_entry_set_text(GTK_ENTRY(state->storage_entry), state->storage_directory);
    gtk_entry_set_text(GTK_ENTRY(state->key_entry), "00112233445566778899aabbccddeeff");
    gui_set_editor_text(state, "", 0);

    gtk_widget_show_all(state->window);
}

static void on_activate(GtkApplication *application, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    char error_message[SECURE_FILE_ERROR_MAX];
    int ret;

    memset(error_message, 0, sizeof(error_message));
    ret = secure_storage_resolve_directory(NULL,
                                           state->storage_directory,
                                           sizeof(state->storage_directory),
                                           error_message,
                                           sizeof(error_message));

    gui_load_css();
    build_gui(application, state);

    if (ret != 0) {
        gui_set_status(state,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                       TRUE);
        gui_append_log(state, "%s",
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        return;
    }

    gtk_entry_set_text(GTK_ENTRY(state->storage_entry), state->storage_directory);
    gui_refresh_file_list(state, FALSE);
    gui_set_status(state,
                   "Giao diện sẵn sàng. Bạn có thể tạo, mở, sửa và xóa tệp trong kho lưu trữ bảo mật.",
                   FALSE);
    gui_append_log(state, "Ứng dụng đã khởi động. Kho lưu trữ bảo mật: %s",
                   state->storage_directory);
}

int main(int argc, char *argv[])
{
    GtkApplication *application;
    secure_file_gui_state *state;
    int status;

    state = g_new0(secure_file_gui_state, 1);
    application = gtk_application_new("com.openai.securefilemanager",
                                      G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), state);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    g_free(state);
    return status;
}
