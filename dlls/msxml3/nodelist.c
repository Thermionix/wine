/*
 *    Node list implementation
 *
 * Copyright 2005 Mike McCormack
 *
 * iface library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * iface library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define COBJMACROS

#include "config.h"

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "ole2.h"
#include "ocidl.h"
#include "msxml.h"
#include "xmldom.h"
#include "msxml.h"

#include "msxml_private.h"

#include "wine/debug.h"

#ifdef HAVE_LIBXML2

WINE_DEFAULT_DEBUG_CHANNEL(msxml);

typedef struct _xmlnodelist
{
    const struct IXMLDOMNodeListVtbl *lpVtbl;
    LONG ref;
    xmlNodePtr node;
    xmlNodePtr current;
} xmlnodelist;

static inline xmlnodelist *impl_from_IXMLDOMNodeList( IXMLDOMNodeList *iface )
{
    return (xmlnodelist *)((char*)iface - FIELD_OFFSET(xmlnodelist, lpVtbl));
}

static HRESULT WINAPI xmlnodelist_QueryInterface(
    IXMLDOMNodeList *iface,
    REFIID riid,
    void** ppvObject )
{
    TRACE("%p %p %p\n", iface, debugstr_guid(riid), ppvObject);

    if ( IsEqualGUID( riid, &IID_IUnknown ) ||
         IsEqualGUID( riid, &IID_IDispatch ) ||
         IsEqualGUID( riid, &IID_IXMLDOMNodeList ) )
    {
        *ppvObject = iface;
    }
    else
        return E_NOINTERFACE;

    IXMLDOMNodeList_AddRef( iface );

    return S_OK;
}

static ULONG WINAPI xmlnodelist_AddRef(
    IXMLDOMNodeList *iface )
{
    xmlnodelist *This = impl_from_IXMLDOMNodeList( iface );
    return InterlockedIncrement( &This->ref );
}

static ULONG WINAPI xmlnodelist_Release(
    IXMLDOMNodeList *iface )
{
    xmlnodelist *This = impl_from_IXMLDOMNodeList( iface );
    ULONG ref;

    ref = InterlockedDecrement( &This->ref );
    if ( ref == 0 )
    {
        HeapFree( GetProcessHeap(), 0, This );
    }

    return ref;
}

static HRESULT WINAPI xmlnodelist_GetTypeInfoCount(
    IXMLDOMNodeList *iface,
    UINT* pctinfo )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI xmlnodelist_GetTypeInfo(
    IXMLDOMNodeList *iface,
    UINT iTInfo,
    LCID lcid,
    ITypeInfo** ppTInfo )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI xmlnodelist_GetIDsOfNames(
    IXMLDOMNodeList *iface,
    REFIID riid,
    LPOLESTR* rgszNames,
    UINT cNames,
    LCID lcid,
    DISPID* rgDispId )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI xmlnodelist_Invoke(
    IXMLDOMNodeList *iface,
    DISPID dispIdMember,
    REFIID riid,
    LCID lcid,
    WORD wFlags,
    DISPPARAMS* pDispParams,
    VARIANT* pVarResult,
    EXCEPINFO* pExcepInfo,
    UINT* puArgErr )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI xmlnodelist_get_item(
        IXMLDOMNodeList* iface,
        long index,
        IXMLDOMNode** listItem)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI xmlnodelist_get_length(
        IXMLDOMNodeList* iface,
        long* listLength)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI xmlnodelist_nextNode(
        IXMLDOMNodeList* iface,
        IXMLDOMNode** nextItem)
{
    xmlnodelist *This = impl_from_IXMLDOMNodeList( iface );

    TRACE("%p %p\n", This, nextItem );

    if (!This->current)
        return S_FALSE;

    *nextItem = create_node( This->current );
    This->current = This->current->next;
    return S_OK;
}

static HRESULT WINAPI xmlnodelist_reset(
        IXMLDOMNodeList* iface)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI xmlnodelist__newEnum(
        IXMLDOMNodeList* iface,
        IUnknown** ppUnk)
{
    FIXME("\n");
    return E_NOTIMPL;
}


static const struct IXMLDOMNodeListVtbl xmlnodelist_vtbl =
{
    xmlnodelist_QueryInterface,
    xmlnodelist_AddRef,
    xmlnodelist_Release,
    xmlnodelist_GetTypeInfoCount,
    xmlnodelist_GetTypeInfo,
    xmlnodelist_GetIDsOfNames,
    xmlnodelist_Invoke,
    xmlnodelist_get_item,
    xmlnodelist_get_length,
    xmlnodelist_nextNode,
    xmlnodelist_reset,
    xmlnodelist__newEnum,
};

IXMLDOMNodeList* create_nodelist( xmlNodePtr node )
{
    xmlnodelist *nodelist;

    nodelist = HeapAlloc( GetProcessHeap(), 0, sizeof *nodelist );
    if ( !nodelist )
        return NULL;

    nodelist->lpVtbl = &xmlnodelist_vtbl;
    nodelist->ref = 1;
    nodelist->node = node;
    nodelist->current = node;

    return (IXMLDOMNodeList*) &nodelist->lpVtbl;
}

#endif
