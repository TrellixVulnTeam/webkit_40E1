/*
 * Copyright (C) 2011 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebPopupMenuProxyGtk.h"

#include "NativeWebMouseEvent.h"
#include "WebPopupItem.h"
#include <WebCore/GtkUtilities.h>
#include <WebCore/IntRect.h>
#include <gtk/gtk.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/CString.h>

using namespace WebCore;

namespace WebKit {

WebPopupMenuProxyGtk::WebPopupMenuProxyGtk(GtkWidget* webView, WebPopupMenuProxy::Client& client)
    : WebPopupMenuProxy(client)
    , m_webView(webView)
    , m_popup(gtk_menu_new())
    , m_dismissMenuTimer(RunLoop::main(), this, &WebPopupMenuProxyGtk::dismissMenuTimerFired)
{
    g_signal_connect(m_popup, "key-press-event", G_CALLBACK(keyPressEventCallback), this);
    g_signal_connect(m_popup, "unmap", G_CALLBACK(menuUnmappedCallback), this);
}

WebPopupMenuProxyGtk::~WebPopupMenuProxyGtk()
{
    cancelTracking();
}

GtkAction* WebPopupMenuProxyGtk::createGtkActionForMenuItem(const WebPopupItem& item, int itemIndex)
{
    GUniquePtr<char> actionName(g_strdup_printf("popup-menu-action-%d", itemIndex));
    GtkAction* action = gtk_action_new(actionName.get(), item.m_text.utf8().data(), item.m_toolTip.utf8().data(), 0);
    g_object_set_data(G_OBJECT(action), "popup-menu-action-index", GINT_TO_POINTER(itemIndex));
    g_signal_connect(action, "activate", G_CALLBACK(menuItemActivated), this);
    gtk_action_set_sensitive(action, item.m_isEnabled);

    return action;
}

void WebPopupMenuProxyGtk::populatePopupMenu(const Vector<WebPopupItem>& items)
{
    int itemIndex = 0;
    for (const auto& item : items) {
        if (item.m_type == WebPopupItem::Separator) {
            GtkWidget* menuItem = gtk_separator_menu_item_new();
            gtk_menu_shell_append(GTK_MENU_SHELL(m_popup), menuItem);
            gtk_widget_show(menuItem);
        } else {
            GRefPtr<GtkAction> action = adoptGRef(createGtkActionForMenuItem(item, itemIndex));
            GtkWidget* menuItem = gtk_action_create_menu_item(action.get());
            gtk_widget_set_tooltip_text(menuItem, gtk_action_get_tooltip(action.get()));
            g_signal_connect(menuItem, "select", G_CALLBACK(selectItemCallback), this);
            gtk_menu_shell_append(GTK_MENU_SHELL(m_popup), menuItem);

            if (gtk_action_is_visible(action.get()))
                gtk_widget_show(menuItem);
        }
        itemIndex++;
    }
}

void WebPopupMenuProxyGtk::showPopupMenu(const IntRect& rect, TextDirection, double /* pageScaleFactor */, const Vector<WebPopupItem>& items, const PlatformPopupMenuData&, int32_t selectedIndex)
{
    m_dismissMenuTimer.stop();

    populatePopupMenu(items);
    gtk_menu_set_active(GTK_MENU(m_popup), selectedIndex);

    resetTypeAheadFindState();

    // This approach follows the one in gtkcombobox.c.
    GtkRequisition requisition;
    gtk_widget_set_size_request(m_popup, -1, -1);
    gtk_widget_get_preferred_size(m_popup, &requisition, nullptr);
    gtk_widget_set_size_request(m_popup, std::max(rect.width(), requisition.width), -1);

    // Reposition the menu after giving it a new width.
    gtk_menu_reposition(GTK_MENU(m_popup));

#if GTK_CHECK_VERSION(3, 22, 0)
    // With a recent GTK+ we calculate an offset from the position where the menu would
    // be normally popped up, and use it as value of the "rect-anchor-dy" property.
    // The code in gtkcombobox.c starts with the offset hardcoded as -2 as well.
    IntPoint menuPosition { 0, -2 };
#else
    IntPoint menuPosition = convertWidgetPointToScreenPoint(m_webView, rect.location());
    menuPosition.move(0, rect.height());
#endif

    if (int itemCount = items.size()) {
        GUniquePtr<GList> children(gtk_container_get_children(GTK_CONTAINER(m_popup)));
        int i;
        GList* child;
        for (i = 0, child = children.get(); i < itemCount; i++, child = g_list_next(child)) {
#if GTK_CHECK_VERSION(3, 22, 0)
            // Do not count the last one: we are using the top-left corner of the
            // item (GDK_GRAVITY_NORTH_WEST) as reference point of the popup.
            if (i >= selectedIndex)
                break;
#else
            if (i > selectedIndex)
                break;
#endif

            GtkWidget* item = GTK_WIDGET(child->data);
            GtkRequisition itemRequisition;
            gtk_widget_get_preferred_size(item, &itemRequisition, nullptr);
            menuPosition.setY(menuPosition.y() - itemRequisition.height);
        }
    } else {
        // Center vertically the empty popup in the combo box area.
        menuPosition.setY(menuPosition.y() - rect.height() / 2);
    }

    gtk_menu_attach_to_widget(GTK_MENU(m_popup), GTK_WIDGET(m_webView), nullptr);

    const GdkEvent* event = m_client->currentlyProcessedMouseDownEvent() ? m_client->currentlyProcessedMouseDownEvent()->nativeEvent() : nullptr;
#if GTK_CHECK_VERSION(3, 22, 0)
    // Set the same properties that GTK+ uses itself for combo box popups.
    g_object_set(m_popup, "menu-type-hint", GDK_WINDOW_TYPE_HINT_COMBO, "rect-anchor-dy", menuPosition.y(), "anchor-hints", GDK_ANCHOR_SLIDE | GDK_ANCHOR_RESIZE, nullptr);
    const GdkRectangle referenceRect = { rect.x(), rect.y(), rect.width(), rect.height() };
    gtk_menu_popup_at_rect(GTK_MENU(m_popup), gtk_widget_get_window(m_webView), &referenceRect, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, event);
#else
    gtk_menu_popup_for_device(GTK_MENU(m_popup), event ? gdk_event_get_device(event) : nullptr, nullptr, nullptr,
        [](GtkMenu*, gint* x, gint* y, gboolean* pushIn, gpointer userData) {
            // We can pass a pointer to the menuPosition local variable because the nested main loop ensures this is called in the function context.
            IntPoint* menuPosition = static_cast<IntPoint*>(userData);
            *x = menuPosition->x();
            *y = menuPosition->y();
            *pushIn = menuPosition->y() < 0;
        }, &menuPosition, nullptr, event && event->type == GDK_BUTTON_PRESS ? event->button.button : 1,
        event ? gdk_event_get_time(event) : GDK_CURRENT_TIME);
#endif

    // Now that the menu has a position, schedule a resize to make sure it's resized to fit vertically in the work area.
    gtk_widget_queue_resize(m_popup);

    // PopupMenu can fail to open when there is no mouse grab.
    // Ensure WebCore does not go into some pesky state.
    if (!gtk_widget_get_visible(m_popup)) {
       m_client->failedToShowPopupMenu();
       return;
    }

    // This ensures that the active item gets selected after popping up the menu, and
    // as it says in "gtkcombobox.c" (line ~1606): it's ugly, but gets the job done.
    GtkWidget* activeChild = gtk_menu_get_active(GTK_MENU(m_popup));
    if (activeChild && gtk_widget_get_visible(activeChild))
        gtk_menu_shell_select_item(GTK_MENU_SHELL(m_popup), activeChild);
}

void WebPopupMenuProxyGtk::hidePopupMenu()
{
    gtk_menu_popdown(GTK_MENU(m_popup));
    resetTypeAheadFindState();
}

void WebPopupMenuProxyGtk::cancelTracking()
{
    if (!m_popup)
        return;

    m_dismissMenuTimer.stop();
    g_signal_handlers_disconnect_matched(m_popup, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
    hidePopupMenu();
    gtk_widget_destroy(m_popup);
    m_popup = nullptr;
}

bool WebPopupMenuProxyGtk::typeAheadFind(GdkEventKey* event)
{
    // If we were given a non-printable character just skip it.
    gunichar unicodeCharacter = gdk_keyval_to_unicode(event->keyval);
    if (!g_unichar_isprint(unicodeCharacter)) {
        resetTypeAheadFindState();
        return false;
    }

    glong charactersWritten;
    GUniquePtr<gunichar2> utf16String(g_ucs4_to_utf16(&unicodeCharacter, 1, nullptr, &charactersWritten, nullptr));
    if (!utf16String) {
        resetTypeAheadFindState();
        return false;
    }

    // If the character is the same as the last character, the user is probably trying to
    // cycle through the menulist entries. This matches the WebCore behavior for collapsed menulists.
    static const uint32_t searchTimeoutMs = 1000;
    bool repeatingCharacter = unicodeCharacter != m_previousKeyEventCharacter;
    if (event->time - m_previousKeyEventTimestamp > searchTimeoutMs)
        m_currentSearchString = String(reinterpret_cast<UChar*>(utf16String.get()), charactersWritten);
    else if (repeatingCharacter)
        m_currentSearchString.append(String(reinterpret_cast<UChar*>(utf16String.get()), charactersWritten));

    m_previousKeyEventTimestamp = event->time;
    m_previousKeyEventCharacter = unicodeCharacter;

    GUniquePtr<GList> children(gtk_container_get_children(GTK_CONTAINER(m_popup)));
    if (!children)
        return true;

    // We case fold before searching, because strncmp does not handle non-ASCII characters.
    GUniquePtr<gchar> searchStringWithCaseFolded(g_utf8_casefold(m_currentSearchString.utf8().data(), -1));
    size_t prefixLength = strlen(searchStringWithCaseFolded.get());

    // If a menu item has already been selected, start searching from the current
    // item down the list. This will make multiple key presses of the same character
    // advance the selection.
    GList* currentChild = children.get();
    if (m_currentlySelectedMenuItem) {
        currentChild = g_list_find(children.get(), m_currentlySelectedMenuItem);
        if (!currentChild) {
            m_currentlySelectedMenuItem = nullptr;
            currentChild = children.get();
        }

        // Repeating characters should iterate.
        if (repeatingCharacter) {
            if (GList* nextChild = g_list_next(currentChild))
                currentChild = nextChild;
        }
    }

    GList* firstChild = currentChild;
    do {
        currentChild = g_list_next(currentChild);
        if (!currentChild)
            currentChild = children.get();

        GUniquePtr<gchar> itemText(g_utf8_casefold(gtk_menu_item_get_label(GTK_MENU_ITEM(currentChild->data)), -1));
        if (!strncmp(searchStringWithCaseFolded.get(), itemText.get(), prefixLength)) {
            gtk_menu_shell_select_item(GTK_MENU_SHELL(m_popup), GTK_WIDGET(currentChild->data));
            break;
        }
    } while (currentChild != firstChild);

    return true;
}

void WebPopupMenuProxyGtk::resetTypeAheadFindState()
{
    m_currentlySelectedMenuItem = nullptr;
    m_previousKeyEventCharacter = 0;
    m_previousKeyEventTimestamp = 0;
    m_currentSearchString = emptyString();
}

void WebPopupMenuProxyGtk::menuItemActivated(GtkAction* action, WebPopupMenuProxyGtk* popupMenu)
{
    popupMenu->m_dismissMenuTimer.stop();
    if (popupMenu->m_client)
        popupMenu->m_client->valueChangedForPopupMenu(popupMenu, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "popup-menu-action-index")));
}

void WebPopupMenuProxyGtk::dismissMenuTimerFired()
{
    if (m_client)
        m_client->valueChangedForPopupMenu(this, -1);
}

void WebPopupMenuProxyGtk::menuUnmappedCallback(GtkWidget*, WebPopupMenuProxyGtk* popupMenu)
{
    if (!popupMenu->m_client)
        return;

    // When an item is activated, the menu is first hidden and then activate signal is emitted, so at this point we don't know
    // if the menu has been hidden because an item has been selected or because the menu has been dismissed. Wait until the next
    // main loop iteration to dismiss the menu, if an item is activated the timer will be cancelled.
    popupMenu->m_dismissMenuTimer.startOneShot(0_s);
}

void WebPopupMenuProxyGtk::selectItemCallback(GtkWidget* item, WebPopupMenuProxyGtk* popupMenu)
{
    popupMenu->setCurrentlySelectedMenuItem(item);
}

gboolean WebPopupMenuProxyGtk::keyPressEventCallback(GtkWidget*, GdkEventKey* event, WebPopupMenuProxyGtk* popupMenu)
{
    return popupMenu->typeAheadFind(event);
}

} // namespace WebKit
