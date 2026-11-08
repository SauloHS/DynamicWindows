#include "app.h"

#include <math.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct Accessible {
    IAccessible client;
    volatile LONG references;
    App *app;
    ITypeInfo *type_info;
} Accessible;

static Accessible *object(IAccessible *client) {
    return (Accessible *)client;
}

static int child_index(Accessible *accessible, VARIANT child) {
    if (!accessible->app || child.vt != VT_I4 || child.lVal < 0 ||
        child.lVal > accessible->app->hit_count)
        return -2;
    return child.lVal == CHILDID_SELF ? -1 : (int)child.lVal - 1;
}

static HRESULT string_result(BSTR *result, const wchar_t *value) {
    if (!result)
        return E_POINTER;
    *result = value ? SysAllocString(value) : NULL;
    return value && !*result ? E_OUTOFMEMORY : value ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE accessible_query(IAccessible *client, REFIID iid, void **result) {
    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IDispatch) &&
        !IsEqualIID(iid, &IID_IAccessible))
        return E_NOINTERFACE;
    *result = client;
    IAccessible_AddRef(client);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE accessible_add_ref(IAccessible *client) {
    return (ULONG)InterlockedIncrement(&object(client)->references);
}

static ULONG STDMETHODCALLTYPE accessible_release(IAccessible *client) {
    Accessible *accessible = object(client);
    LONG references = InterlockedDecrement(&accessible->references);
    if (references == 0) {
        if (accessible->type_info)
            ITypeInfo_Release(accessible->type_info);
        free(accessible);
    }
    return (ULONG)references;
}

static HRESULT STDMETHODCALLTYPE accessible_type_count(IAccessible *client, UINT *count) {
    if (!count)
        return E_POINTER;
    *count = object(client)->type_info ? 1 : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_type(IAccessible *client, UINT index, LCID locale,
                                                 ITypeInfo **result) {
    (void)locale;
    if (!result)
        return E_POINTER;
    *result = NULL;
    if (index != 0)
        return DISP_E_BADINDEX;
    ITypeInfo *info = object(client)->type_info;
    if (!info)
        return E_NOTIMPL;
    ITypeInfo_AddRef(info);
    *result = info;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_names(IAccessible *client, REFIID iid, LPOLESTR *names,
                                                  UINT count, LCID locale, DISPID *ids) {
    (void)iid;
    (void)locale;
    ITypeInfo *info = object(client)->type_info;
    return info ? DispGetIDsOfNames(info, names, count, ids) : DISP_E_UNKNOWNNAME;
}

static HRESULT STDMETHODCALLTYPE accessible_invoke(IAccessible *client, DISPID member, REFIID iid,
                                                   LCID locale, WORD flags, DISPPARAMS *parameters,
                                                   VARIANT *result, EXCEPINFO *exception,
                                                   UINT *argument_error) {
    (void)iid;
    (void)locale;
    ITypeInfo *info = object(client)->type_info;
    return info ? DispInvoke(client, info, member, flags, parameters, result, exception,
                             argument_error)
                : DISP_E_MEMBERNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE accessible_parent(IAccessible *client, IDispatch **result) {
    Accessible *accessible = object(client);
    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!accessible->app)
        return CO_E_OBJNOTCONNECTED;
    return CreateStdAccessibleObject(accessible->app->window, OBJID_WINDOW, &IID_IDispatch,
                                     (void **)result);
}

static HRESULT STDMETHODCALLTYPE accessible_count(IAccessible *client, LONG *count) {
    if (!count)
        return E_POINTER;
    App *app = object(client)->app;
    *count = app ? app->hit_count : 0;
    return app ? S_OK : CO_E_OBJNOTCONNECTED;
}

static HRESULT STDMETHODCALLTYPE accessible_child(IAccessible *client, VARIANT child,
                                                  IDispatch **result) {
    if (!result)
        return E_POINTER;
    *result = NULL;
    return child_index(object(client), child) < -1 ? E_INVALIDARG : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE accessible_name(IAccessible *client, VARIANT child, BSTR *result) {
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1)
        return string_result(result, NULL) == E_POINTER ? E_POINTER : E_INVALIDARG;
    if (index >= 0)
        return string_result(result, accessible->app->hits[index].label);
    wchar_t summary[600];
    app_summary(accessible->app, summary, 600);
    return string_result(result, summary);
}

static HRESULT STDMETHODCALLTYPE accessible_value(IAccessible *client, VARIANT child,
                                                  BSTR *result) {
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1)
        return E_INVALIDARG;
    if (index == -1)
        return string_result(result, NULL);
    App *app = accessible->app;
    wchar_t value[64];
    Action action = app->hits[index].action;
    if (action == ACTION_VOLUME) {
        swprintf(value, 64, L"%d%%", (int)lround(app->system.volume * 100));
        return string_result(result, value);
    }
    if (action == ACTION_SEEK) {
        app_clock((uint64_t)(app->media.position * 1000), value, 64);
        return string_result(result, value);
    }
    return string_result(result, NULL);
}

static HRESULT STDMETHODCALLTYPE accessible_description(IAccessible *client, VARIANT child,
                                                        BSTR *result) {
    int index = child_index(object(client), child);
    if (index < -1)
        return E_INVALIDARG;
    return string_result(result, index == -1
                                     ? L"Dynamic activity island. Tab moves between controls. "
                                       L"Escape collapses the island."
                                     : NULL);
}

static HRESULT STDMETHODCALLTYPE accessible_role(IAccessible *client, VARIANT child,
                                                 VARIANT *result) {
    if (!result)
        return E_POINTER;
    VariantInit(result);
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1)
        return E_INVALIDARG;
    result->vt = VT_I4;
    result->lVal = ROLE_SYSTEM_PANE;
    if (index < 0)
        return S_OK;
    Action action = accessible->app->hits[index].action;
    result->lVal = action == ACTION_VOLUME || action == ACTION_SEEK ? ROLE_SYSTEM_SLIDER
                   : (action >= ACTION_TAB_MEDIA && action <= ACTION_TAB_SYSTEM) ||
                           action == ACTION_TAB_NOTIFICATIONS
                       ? ROLE_SYSTEM_PAGETAB
                       : ROLE_SYSTEM_PUSHBUTTON;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_state(IAccessible *client, VARIANT child,
                                                  VARIANT *result) {
    if (!result)
        return E_POINTER;
    VariantInit(result);
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1)
        return E_INVALIDARG;
    App *app = accessible->app;
    result->vt = VT_I4;
    result->lVal = STATE_SYSTEM_FOCUSABLE;
    if (!IsWindowVisible(app->window))
        result->lVal |= STATE_SYSTEM_INVISIBLE;
    if (index < 0) {
        if (GetForegroundWindow() == app->window && app->focus < 0)
            result->lVal |= STATE_SYSTEM_FOCUSED;
        result->lVal |= app->island.expanded ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
        return S_OK;
    }
    UiHit *hit = &app->hits[index];
    if (!hit->enabled)
        result->lVal |= STATE_SYSTEM_UNAVAILABLE;
    if (app->focus == index && GetForegroundWindow() == app->window)
        result->lVal |= STATE_SYSTEM_FOCUSED;
    if ((int)hit->action == ACTION_TAB_MEDIA + (int)app->island.view)
        result->lVal |= STATE_SYSTEM_SELECTED;
    if (app->pressed == hit->action)
        result->lVal |= STATE_SYSTEM_PRESSED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_help(IAccessible *client, VARIANT child, BSTR *result) {
    if (child_index(object(client), child) < -1)
        return E_INVALIDARG;
    return string_result(result, NULL);
}

static HRESULT STDMETHODCALLTYPE accessible_help_topic(IAccessible *client, BSTR *file,
                                                       VARIANT child, LONG *topic) {
    if (file)
        *file = NULL;
    if (topic)
        *topic = 0;
    return child_index(object(client), child) < -1 ? E_INVALIDARG : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE accessible_shortcut(IAccessible *client, VARIANT child,
                                                     BSTR *result) {
    int index = child_index(object(client), child);
    if (index < -1)
        return E_INVALIDARG;
    if (index >= 0)
        return string_result(result, NULL);
    wchar_t text[128];
    config_shortcut_text(object(client)->app->config.shortcuts[SHORTCUT_ISLAND], text, 128);
    return string_result(result, text);
}

static HRESULT STDMETHODCALLTYPE accessible_focus(IAccessible *client, VARIANT *result) {
    if (!result)
        return E_POINTER;
    VariantInit(result);
    App *app = object(client)->app;
    if (!app)
        return CO_E_OBJNOTCONNECTED;
    if (GetForegroundWindow() != app->window)
        return S_FALSE;
    result->vt = VT_I4;
    result->lVal = app->focus + 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_selection(IAccessible *client, VARIANT *result) {
    if (!result)
        return E_POINTER;
    VariantInit(result);
    App *app = object(client)->app;
    if (!app)
        return CO_E_OBJNOTCONNECTED;
    for (int i = 0; i < app->hit_count; ++i)
        if ((int)app->hits[i].action == ACTION_TAB_MEDIA + (int)app->island.view) {
            result->vt = VT_I4;
            result->lVal = i + 1;
            return S_OK;
        }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE accessible_default(IAccessible *client, VARIANT child,
                                                    BSTR *result) {
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1)
        return E_INVALIDARG;
    if (index < 0)
        return string_result(result, L"Expand");
    Action action = accessible->app->hits[index].action;
    return string_result(result,
                         action == ACTION_VOLUME || action == ACTION_SEEK ? L"Adjust" : L"Press");
}

static HRESULT STDMETHODCALLTYPE accessible_select(IAccessible *client, LONG flags, VARIANT child) {
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1 || !(flags & SELFLAG_TAKEFOCUS))
        return E_INVALIDARG;
    PostMessageW(accessible->app->window, WM_DYNAMICWINDOWS_ACCESS, ACTION_NONE, index);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_location(IAccessible *client, LONG *left, LONG *top,
                                                     LONG *width, LONG *height, VARIANT child) {
    if (!left || !top || !width || !height)
        return E_POINTER;
    *left = *top = *width = *height = 0;
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1)
        return E_INVALIDARG;
    App *app = accessible->app;
    RectF rect = index < 0 ? app->renderer.body : app->hits[index].rect;
    POINT origin = {0, 0};
    ClientToScreen(app->window, &origin);
    *left = origin.x + (LONG)lround(rect.X * app->scale);
    *top = origin.y + (LONG)lround(rect.Y * app->scale);
    *width = (LONG)lround(rect.Width * app->scale);
    *height = (LONG)lround(rect.Height * app->scale);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_navigate(IAccessible *client, LONG direction,
                                                     VARIANT start, VARIANT *result) {
    if (!result)
        return E_POINTER;
    VariantInit(result);
    Accessible *accessible = object(client);
    int index = child_index(accessible, start);
    if (index < -1)
        return E_INVALIDARG;
    int count = accessible->app->hit_count;
    int next = -1;
    if (direction == NAVDIR_FIRSTCHILD && index == -1)
        next = 0;
    else if (direction == NAVDIR_LASTCHILD && index == -1)
        next = count - 1;
    else if (direction == NAVDIR_NEXT)
        next = index + 1;
    else if (direction == NAVDIR_PREVIOUS)
        next = index - 1;
    else
        return E_NOTIMPL;
    if (next < 0 || next >= count)
        return S_FALSE;
    result->vt = VT_I4;
    result->lVal = next + 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_hit_test(IAccessible *client, LONG x, LONG y,
                                                     VARIANT *result) {
    if (!result)
        return E_POINTER;
    VariantInit(result);
    App *app = object(client)->app;
    if (!app)
        return CO_E_OBJNOTCONNECTED;
    POINT point = {x, y};
    ScreenToClient(app->window, &point);
    double local_x = point.x / app->scale;
    double local_y = point.y / app->scale;
    if (!app_inside(app, local_x, local_y))
        return S_FALSE;
    result->vt = VT_I4;
    result->lVal = app_hit(app, local_x, local_y) + 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_do_action(IAccessible *client, VARIANT child) {
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < -1)
        return E_INVALIDARG;
    App *app = accessible->app;
    if (index >= 0 && !app->hits[index].enabled)
        return E_ACCESSDENIED;
    Action action = index < 0 ? ACTION_EXPAND : app->hits[index].action;
    if (action == ACTION_VOLUME || action == ACTION_SEEK)
        return S_FALSE;
    PostMessageW(app->window, WM_DYNAMICWINDOWS_ACCESS, action, 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE accessible_put_name(IAccessible *client, VARIANT child,
                                                     BSTR name) {
    (void)name;
    return child_index(object(client), child) < -1 ? E_INVALIDARG : E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE accessible_put_value(IAccessible *client, VARIANT child,
                                                      BSTR value) {
    Accessible *accessible = object(client);
    int index = child_index(accessible, child);
    if (index < 0 || !value)
        return E_INVALIDARG;
    App *app = accessible->app;
    Action action = app->hits[index].action;
    if (action != ACTION_VOLUME && action != ACTION_SEEK)
        return E_NOTIMPL;
    if (!app->hits[index].enabled)
        return E_ACCESSDENIED;
    wchar_t *end = NULL;
    double number = wcstod(value, &end);
    if (end == value || !isfinite(number) || number < 0)
        return E_INVALIDARG;
    if (*end == L'%')
        ++end;
    if (*end != L'\0')
        return E_INVALIDARG;
    double maximum = action == ACTION_VOLUME ? 100 : app->media.duration;
    if (maximum <= 0 || number > maximum)
        return E_INVALIDARG;
    PostMessageW(app->window, WM_DYNAMICWINDOWS_ACCESS, action,
                 (LPARAM)lround(number / maximum * 10000));
    return S_OK;
}

static IAccessibleVtbl accessible_vtable = {
    accessible_query,       accessible_add_ref,   accessible_release,  accessible_type_count,
    accessible_type,        accessible_names,     accessible_invoke,   accessible_parent,
    accessible_count,       accessible_child,     accessible_name,     accessible_value,
    accessible_description, accessible_role,      accessible_state,    accessible_help,
    accessible_help_topic,  accessible_shortcut,  accessible_focus,    accessible_selection,
    accessible_default,     accessible_select,    accessible_location, accessible_navigate,
    accessible_hit_test,    accessible_do_action, accessible_put_name, accessible_put_value};

IAccessible *accessibility_create(App *app) {
    Accessible *accessible = calloc(1, sizeof(*accessible));
    if (!accessible)
        return NULL;
    accessible->client.lpVtbl = &accessible_vtable;
    accessible->references = 1;
    accessible->app = app;
    ITypeLib *library = NULL;
    if (SUCCEEDED(LoadTypeLib(L"oleacc.dll", &library))) {
        ITypeLib_GetTypeInfoOfGuid(library, &IID_IAccessible, &accessible->type_info);
        ITypeLib_Release(library);
    }
    app->accessible = accessible;
    return &accessible->client;
}

void accessibility_disconnect(App *app) {
    Accessible *accessible = app->accessible;
    app->accessible = NULL;
    if (!accessible)
        return;
    accessible->app = NULL;
    IAccessible_Release(&accessible->client);
}

LRESULT accessibility_object(App *app, WPARAM wparam, LPARAM lparam) {
    if ((LONG)lparam != OBJID_CLIENT)
        return 0;
    if (!app->accessible && !accessibility_create(app))
        return 0;
    return LresultFromObject(&IID_IAccessible, wparam, (IUnknown *)&app->accessible->client);
}
