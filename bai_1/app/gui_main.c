#include <gtk/gtk.h>

#include "file_manager.h"
#include "secure_file_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    COLUMN_NAME = 0,
    COLUMN_TYPE,
    COLUMN_SIZE,
    COLUMN_MODIFIED,
    COLUMN_PATH,
    COLUMN_IS_DIR,
    COLUMN_COUNT
};

typedef struct secure_file_gui_state {
    GtkWidget *window;
    GtkWidget *directory_entry;
    GtkWidget *tree_view;
    GtkListStore *list_store;
    GtkWidget *input_entry;
    GtkWidget *output_entry;
    GtkWidget *device_entry;
    GtkWidget *mode_combo;
    GtkWidget *key_entry;
    GtkWidget *iv_entry;
    GtkWidget *status_label;
    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;
    GtkWidget *delete_button;
    GtkWidget *use_button;
    GtkWidget *process_button;
    char current_directory[FILE_MANAGER_PATH_MAX];
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

static uint32_t gui_get_selected_mode(secure_file_gui_state *state)
{
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(state->mode_combo));
    return (active == 1) ? SECURE_AES_MODE_DECRYPT : SECURE_AES_MODE_ENCRYPT;
}

static void gui_maybe_suggest_output(secure_file_gui_state *state, gboolean overwrite)
{
    const char *input_path;
    const char *current_output;
    char suggestion[FILE_MANAGER_PATH_MAX];
    int ret;

    input_path = gtk_entry_get_text(GTK_ENTRY(state->input_entry));
    current_output = gtk_entry_get_text(GTK_ENTRY(state->output_entry));

    if (!input_path || input_path[0] == '\0')
        return;

    if (!overwrite && current_output && current_output[0] != '\0')
        return;

    ret = secure_file_suggest_output_path(input_path,
                                          gui_get_selected_mode(state),
                                          suggestion,
                                          sizeof(suggestion));
    if (ret == 0)
        gtk_entry_set_text(GTK_ENTRY(state->output_entry), suggestion);
}

static gboolean gui_get_selected_item(secure_file_gui_state *state,
                                      gchar **path,
                                      gboolean *is_directory)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!state || !path || !is_directory)
        return FALSE;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return FALSE;

    gtk_tree_model_get(model, &iter,
                       COLUMN_PATH, path,
                       COLUMN_IS_DIR, is_directory,
                       -1);
    return (*path != NULL);
}

static void gui_update_selection_buttons(secure_file_gui_state *state)
{
    GtkTreeSelection *selection;
    gboolean has_selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    has_selection = gtk_tree_selection_count_selected_rows(selection) > 0;

    gtk_widget_set_sensitive(state->use_button, has_selection);
    gtk_widget_set_sensitive(state->delete_button, has_selection);
}

static void gui_populate_directory(secure_file_gui_state *state,
                                   const char *directory_path,
                                   gboolean log_refresh)
{
    struct file_manager_entry *entries = NULL;
    size_t entry_count = 0;
    char normalized[FILE_MANAGER_PATH_MAX];
    char error_message[SECURE_FILE_ERROR_MAX];
    size_t index;
    int ret;

    memset(error_message, 0, sizeof(error_message));
    ret = file_manager_normalize_directory(directory_path,
                                           normalized,
                                           sizeof(normalized),
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

    ret = file_manager_list_directory(normalized,
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

        size_text = entries[index].is_directory
                        ? g_strdup("-")
                        : g_format_size((goffset)entries[index].size_bytes);

        gtk_list_store_append(state->list_store, &iter);
        gtk_list_store_set(state->list_store, &iter,
                           COLUMN_NAME, entries[index].name,
                           COLUMN_TYPE, entries[index].type_label,
                           COLUMN_SIZE, size_text,
                           COLUMN_MODIFIED, modified_text,
                           COLUMN_PATH, entries[index].path,
                           COLUMN_IS_DIR, entries[index].is_directory,
                           -1);
        g_free(size_text);
    }

    snprintf(state->current_directory, sizeof(state->current_directory), "%s", normalized);
    gtk_entry_set_text(GTK_ENTRY(state->directory_entry), normalized);
    gui_set_status(state, "Directory refreshed", FALSE);
    if (log_refresh)
        gui_append_log(state, "Loaded %zu item(s) from %s", entry_count, normalized);

    file_manager_free_entries(entries);
    gui_update_selection_buttons(state);
}

static void gui_use_selected_item(secure_file_gui_state *state)
{
    gchar *selected_path = NULL;
    gboolean is_directory = FALSE;

    if (!gui_get_selected_item(state, &selected_path, &is_directory)) {
        gui_set_status(state, "Select a file or folder first", TRUE);
        return;
    }

    if (is_directory) {
        gui_populate_directory(state, selected_path, TRUE);
        gui_append_log(state, "Opened folder %s", selected_path);
    } else {
        gtk_entry_set_text(GTK_ENTRY(state->input_entry), selected_path);
        gui_maybe_suggest_output(state, FALSE);
        gui_set_status(state, "Selected input file", FALSE);
        gui_append_log(state, "Selected input file %s", selected_path);
    }

    g_free(selected_path);
}

static void on_tree_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    (void)selection;
    gui_update_selection_buttons((secure_file_gui_state *)user_data);
}

static void on_tree_row_activated(GtkTreeView *tree_view,
                                  GtkTreePath *path,
                                  GtkTreeViewColumn *column,
                                  gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    (void)tree_view;
    (void)path;
    (void)column;
    gui_use_selected_item(state);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    (void)button;
    gui_populate_directory(state,
                           gtk_entry_get_text(GTK_ENTRY(state->directory_entry)),
                           TRUE);
}

static void on_browse_directory_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    GtkWidget *dialog;

    (void)button;
    dialog = gtk_file_chooser_dialog_new("Select Folder",
                                         GTK_WINDOW(state->window),
                                         GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), state->current_directory);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gui_populate_directory(state, folder, TRUE);
        g_free(folder);
    }

    gtk_widget_destroy(dialog);
}

static void on_up_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    char parent[FILE_MANAGER_PATH_MAX];
    char error_message[SECURE_FILE_ERROR_MAX];
    int ret;

    (void)button;
    memset(error_message, 0, sizeof(error_message));
    ret = file_manager_get_parent_directory(state->current_directory,
                                            parent,
                                            sizeof(parent),
                                            error_message,
                                            sizeof(error_message));
    if (ret != 0) {
        gui_set_status(state,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                       TRUE);
        return;
    }

    gui_populate_directory(state, parent, TRUE);
}

static void on_use_selected_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    gui_use_selected_item((secure_file_gui_state *)user_data);
}

static void on_browse_input_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    GtkWidget *dialog;

    (void)button;
    dialog = gtk_file_chooser_dialog_new("Select Input File",
                                         GTK_WINDOW(state->window),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), state->current_directory);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(state->input_entry), filename);
        gui_maybe_suggest_output(state, FALSE);
        gui_set_status(state, "Input file selected", FALSE);
        gui_append_log(state, "Input file set to %s", filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_browse_output_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    GtkWidget *dialog;
    const char *current_output;

    (void)button;
    dialog = gtk_file_chooser_dialog_new("Select Output File",
                                         GTK_WINDOW(state->window),
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Save", GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), state->current_directory);

    current_output = gtk_entry_get_text(GTK_ENTRY(state->output_entry));
    if (current_output && current_output[0] != '\0')
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), current_output);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(state->output_entry), filename);
        gui_set_status(state, "Output path selected", FALSE);
        gui_append_log(state, "Output file set to %s", filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_suggest_output_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    (void)button;
    gui_maybe_suggest_output(state, TRUE);
}

static void on_input_changed(GtkEditable *editable, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    (void)editable;
    gui_maybe_suggest_output(state, FALSE);
}

static void on_mode_changed(GtkComboBox *combo, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    (void)combo;
    gui_maybe_suggest_output(state, FALSE);
}

static void on_clear_form_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    (void)button;

    gtk_entry_set_text(GTK_ENTRY(state->input_entry), "");
    gtk_entry_set_text(GTK_ENTRY(state->output_entry), "");
    gtk_entry_set_text(GTK_ENTRY(state->key_entry), "");
    gtk_entry_set_text(GTK_ENTRY(state->iv_entry), "");
    gtk_entry_set_text(GTK_ENTRY(state->device_entry), SECURE_AES_DEVICE_NAME);
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->mode_combo), 0);
    gui_set_status(state, "Form cleared", FALSE);
}

static void on_new_folder_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *prompt;
    GtkWidget *entry;

    (void)button;
    dialog = gtk_dialog_new_with_buttons("Create Folder",
                                         GTK_WINDOW(state->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Create", GTK_RESPONSE_ACCEPT,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    prompt = gtk_label_new("Folder name:");
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "example: encrypted_docs");
    gtk_box_pack_start(GTK_BOX(content_area), prompt, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(content_area), entry, FALSE, FALSE, 6);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char error_message[SECURE_FILE_ERROR_MAX];
        const char *folder_name = gtk_entry_get_text(GTK_ENTRY(entry));
        int ret;

        memset(error_message, 0, sizeof(error_message));
        ret = file_manager_create_directory(state->current_directory,
                                            folder_name,
                                            error_message,
                                            sizeof(error_message));
        if (ret != 0) {
            gui_set_status(state,
                           error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                           TRUE);
            gui_append_log(state, "%s",
                           error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        } else {
            gui_set_status(state, "Folder created", FALSE);
            gui_append_log(state, "Created folder %s/%s",
                           state->current_directory, folder_name);
            gui_populate_directory(state, state->current_directory, FALSE);
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_delete_selected_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    gchar *selected_path = NULL;
    gboolean is_directory = FALSE;
    GtkWidget *dialog;

    (void)button;
    if (!gui_get_selected_item(state, &selected_path, &is_directory)) {
        gui_set_status(state, "Select an item to delete", TRUE);
        return;
    }

    dialog = gtk_message_dialog_new(GTK_WINDOW(state->window),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_OK_CANCEL,
                                    "Delete the selected %s?",
                                    is_directory ? "folder and all its contents" : "file");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "%s",
                                             selected_path);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        char error_message[SECURE_FILE_ERROR_MAX];
        int ret;

        memset(error_message, 0, sizeof(error_message));
        ret = file_manager_delete_path(selected_path,
                                       error_message,
                                       sizeof(error_message));
        if (ret != 0) {
            gui_set_status(state,
                           error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                           TRUE);
            gui_append_log(state, "%s",
                           error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        } else {
            gui_set_status(state, "Item deleted", FALSE);
            gui_append_log(state, "Deleted %s", selected_path);
            if (strcmp(gtk_entry_get_text(GTK_ENTRY(state->input_entry)), selected_path) == 0)
                gtk_entry_set_text(GTK_ENTRY(state->input_entry), "");
            gui_populate_directory(state, state->current_directory, FALSE);
        }
    }

    gtk_widget_destroy(dialog);
    g_free(selected_path);
}

static void on_process_clicked(GtkButton *button, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    struct secure_file_request request;
    struct secure_file_result result;
    char error_message[SECURE_FILE_ERROR_MAX];
    int ret;

    (void)button;
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    memset(error_message, 0, sizeof(error_message));

    request.device_path = gtk_entry_get_text(GTK_ENTRY(state->device_entry));
    request.mode = gui_get_selected_mode(state);
    request.input_path = gtk_entry_get_text(GTK_ENTRY(state->input_entry));
    request.output_path = gtk_entry_get_text(GTK_ENTRY(state->output_entry));
    request.key_hex = gtk_entry_get_text(GTK_ENTRY(state->key_entry));
    request.iv_hex = gtk_entry_get_text(GTK_ENTRY(state->iv_entry));

    gtk_widget_set_sensitive(state->process_button, FALSE);
    while (gtk_events_pending())
        gtk_main_iteration();

    ret = secure_file_process_request(&request,
                                      &result,
                                      error_message,
                                      sizeof(error_message));
    gtk_widget_set_sensitive(state->process_button, TRUE);

    if (ret != 0) {
        gui_set_status(state,
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret),
                       TRUE);
        gui_append_log(state, "Operation failed: %s",
                       error_message[0] != '\0' ? error_message : secure_file_describe_error(ret));
        return;
    }

    gui_set_status(state, "Operation completed successfully", FALSE);
    gui_append_log(state,
                   "%s: %s -> %s via %s (%zu byte(s) -> %zu byte(s))",
                   secure_file_mode_to_text(request.mode),
                   request.input_path,
                   request.output_path,
                   request.device_path && request.device_path[0] != '\0'
                       ? request.device_path
                       : SECURE_AES_DEVICE_NAME,
                   result.input_size,
                   result.output_size);

    gui_populate_directory(state, state->current_directory, FALSE);
}

static GtkWidget *create_labeled_entry_row(const char *label_text,
                                           GtkWidget **entry_out,
                                           const char *placeholder,
                                           gboolean monospace)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *entry = gtk_entry_new();

    gtk_widget_set_hexpand(entry, TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
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
    GtkWidget *browser_frame;
    GtkWidget *browser_box;
    GtkWidget *browser_toolbar;
    GtkWidget *browser_scroll;
    GtkWidget *browser_button_row;
    GtkWidget *operation_frame;
    GtkWidget *operation_box;
    GtkWidget *grid;
    GtkWidget *input_box;
    GtkWidget *output_box;
    GtkWidget *input_browse_button;
    GtkWidget *output_browse_button;
    GtkWidget *suggest_button;
    GtkWidget *clear_button;
    GtkWidget *note_label;
    GtkWidget *log_label;
    GtkWidget *log_scroll;
    GtkWidget *browse_button;
    GtkWidget *up_button;
    GtkWidget *refresh_button;
    GtkWidget *new_folder_button;
    GtkWidget *header_bar;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;
    GtkWidget *mode_label;
    GtkWidget *button_row;
    GtkWidget *use_button;

    state->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(state->window), "Secure File Manager");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1200, 760);
    gtk_window_set_position(GTK_WINDOW(state->window), GTK_WIN_POS_CENTER);

    header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Secure File Manager");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), "AES-CBC via kernel driver /dev/secure_aes");
    gtk_window_set_titlebar(GTK_WINDOW(state->window), header_bar);

    root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(root_box), 12);
    gtk_container_add(GTK_CONTAINER(state->window), root_box);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(root_box), paned, TRUE, TRUE, 0);

    browser_frame = gtk_frame_new("File Browser");
    operation_frame = gtk_frame_new("Secure AES Operation");
    gtk_paned_pack1(GTK_PANED(paned), browser_frame, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), operation_frame, TRUE, FALSE);

    browser_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(browser_box), 10);
    gtk_container_add(GTK_CONTAINER(browser_frame), browser_box);

    browser_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->directory_entry = gtk_entry_new();
    gtk_widget_set_hexpand(state->directory_entry, TRUE);
    browse_button = gtk_button_new_with_label("Browse Folder");
    up_button = gtk_button_new_with_label("Up");
    refresh_button = gtk_button_new_with_label("Refresh");
    gtk_box_pack_start(GTK_BOX(browser_toolbar), state->directory_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(browser_toolbar), browse_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(browser_toolbar), up_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(browser_toolbar), refresh_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(browser_box), browser_toolbar, FALSE, FALSE, 0);

    state->list_store = gtk_list_store_new(COLUMN_COUNT,
                                           G_TYPE_STRING,
                                           G_TYPE_STRING,
                                           G_TYPE_STRING,
                                           G_TYPE_STRING,
                                           G_TYPE_STRING,
                                           G_TYPE_BOOLEAN);
    state->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(state->list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(state->tree_view), TRUE);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", COLUMN_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->tree_view), column);
    column = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", COLUMN_TYPE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->tree_view), column);
    column = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", COLUMN_SIZE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->tree_view), column);
    column = gtk_tree_view_column_new_with_attributes("Modified", renderer, "text", COLUMN_MODIFIED, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state->tree_view), column);

    browser_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(browser_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(browser_scroll), state->tree_view);
    gtk_box_pack_start(GTK_BOX(browser_box), browser_scroll, TRUE, TRUE, 0);

    browser_button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    use_button = gtk_button_new_with_label("Use Selected as Input");
    new_folder_button = gtk_button_new_with_label("New Folder");
    state->delete_button = gtk_button_new_with_label("Delete Selected");
    state->use_button = use_button;
    gtk_widget_set_sensitive(state->use_button, FALSE);
    gtk_widget_set_sensitive(state->delete_button, FALSE);
    gtk_box_pack_start(GTK_BOX(browser_button_row), use_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(browser_button_row), new_folder_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(browser_button_row), state->delete_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(browser_box), browser_button_row, FALSE, FALSE, 0);

    operation_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(operation_box), 10);
    gtk_container_add(GTK_CONTAINER(operation_frame), operation_box);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(operation_box), grid, FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(grid), create_labeled_entry_row("Device", &state->device_entry,
                                                             SECURE_AES_DEVICE_NAME, TRUE),
                    0, 0, 3, 1);

    input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(input_box), gtk_label_new("Input"), FALSE, FALSE, 0);
    state->input_entry = gtk_entry_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(state->input_entry), "mono");
    gtk_widget_set_hexpand(state->input_entry, TRUE);
    input_browse_button = gtk_button_new_with_label("Browse File");
    gtk_box_pack_start(GTK_BOX(input_box), state->input_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(input_box), input_browse_button, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), input_box, 0, 1, 3, 1);

    output_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(output_box), gtk_label_new("Output"), FALSE, FALSE, 0);
    state->output_entry = gtk_entry_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(state->output_entry), "mono");
    gtk_widget_set_hexpand(state->output_entry, TRUE);
    output_browse_button = gtk_button_new_with_label("Choose Output");
    gtk_box_pack_start(GTK_BOX(output_box), state->output_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(output_box), output_browse_button, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), output_box, 0, 2, 3, 1);

    mode_label = gtk_label_new("Mode");
    gtk_label_set_xalign(GTK_LABEL(mode_label), 0.0f);
    state->mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->mode_combo), "Encrypt");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->mode_combo), "Decrypt");
    gtk_combo_box_set_active(GTK_COMBO_BOX(state->mode_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), mode_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), state->mode_combo, 1, 3, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), create_labeled_entry_row("AES Key", &state->key_entry,
                                                             "32, 48, or 64 hex chars", TRUE),
                    0, 4, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), create_labeled_entry_row("AES IV", &state->iv_entry,
                                                             "32 hex chars", TRUE),
                    0, 5, 3, 1);

    button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    suggest_button = gtk_button_new_with_label("Suggest Output");
    clear_button = gtk_button_new_with_label("Clear Form");
    state->process_button = gtk_button_new_with_label("Process via Driver");
    gtk_box_pack_start(GTK_BOX(button_row), suggest_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_row), clear_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(button_row), state->process_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(operation_box), button_row, FALSE, FALSE, 0);

    note_label = gtk_label_new("Use a 16/24/32-byte AES key in hex and a 16-byte IV in hex. All AES work stays inside the kernel driver.");
    gtk_label_set_line_wrap(GTK_LABEL(note_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(operation_box), note_label, FALSE, FALSE, 0);

    log_label = gtk_label_new("Operation Log");
    gtk_label_set_xalign(GTK_LABEL(log_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(operation_box), log_label, FALSE, FALSE, 0);

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
    gtk_box_pack_start(GTK_BOX(operation_box), log_scroll, TRUE, TRUE, 0);

    state->status_label = gtk_label_new("");
    gtk_widget_set_name(state->status_label, "status-label");
    gtk_label_set_xalign(GTK_LABEL(state->status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(operation_box), state->status_label, FALSE, FALSE, 0);

    g_signal_connect(browse_button, "clicked", G_CALLBACK(on_browse_directory_clicked), state);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), state);
    g_signal_connect(up_button, "clicked", G_CALLBACK(on_up_clicked), state);
    g_signal_connect(use_button, "clicked", G_CALLBACK(on_use_selected_clicked), state);
    g_signal_connect(new_folder_button, "clicked", G_CALLBACK(on_new_folder_clicked), state);
    g_signal_connect(state->delete_button, "clicked", G_CALLBACK(on_delete_selected_clicked), state);
    g_signal_connect(input_browse_button, "clicked", G_CALLBACK(on_browse_input_clicked), state);
    g_signal_connect(output_browse_button, "clicked", G_CALLBACK(on_browse_output_clicked), state);
    g_signal_connect(suggest_button, "clicked", G_CALLBACK(on_suggest_output_clicked), state);
    g_signal_connect(clear_button, "clicked", G_CALLBACK(on_clear_form_clicked), state);
    g_signal_connect(state->process_button, "clicked", G_CALLBACK(on_process_clicked), state);
    g_signal_connect(state->input_entry, "changed", G_CALLBACK(on_input_changed), state);
    g_signal_connect(state->mode_combo, "changed", G_CALLBACK(on_mode_changed), state);
    g_signal_connect(state->tree_view, "row-activated", G_CALLBACK(on_tree_row_activated), state);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree_view));
    g_signal_connect(selection, "changed", G_CALLBACK(on_tree_selection_changed), state);

    gtk_entry_set_text(GTK_ENTRY(state->device_entry), SECURE_AES_DEVICE_NAME);
    gtk_entry_set_text(GTK_ENTRY(state->key_entry), "00112233445566778899aabbccddeeff");
    gtk_entry_set_text(GTK_ENTRY(state->iv_entry), "0102030405060708090a0b0c0d0e0f10");

    gtk_widget_show_all(state->window);
}

static void on_activate(GtkApplication *application, gpointer user_data)
{
    secure_file_gui_state *state = (secure_file_gui_state *)user_data;
    const char *home_directory = g_get_home_dir();

    gui_load_css();
    build_gui(application, state);
    snprintf(state->current_directory, sizeof(state->current_directory), "%s",
             home_directory ? home_directory : ".");
    gui_populate_directory(state, state->current_directory, FALSE);
    gui_set_status(state, "GUI ready. Load the driver before processing files.", FALSE);
    gui_append_log(state, "Application started. Current directory: %s", state->current_directory);
}

int main(int argc, char *argv[])
{
    GtkApplication *application;
    secure_file_gui_state *state;
    int status;

    state = g_new0(secure_file_gui_state, 1);
    application = gtk_application_new("com.openai.securefilemanager", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(application, "activate", G_CALLBACK(on_activate), state);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    g_free(state);
    return status;
}