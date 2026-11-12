#include "shelf_drag.h"
#include <cstring>

namespace noitch {
namespace {
class FormatEnumerator final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IEnumFORMATETC> {
public:
    FORMATETC format{};
    bool consumed = false;
    IFACEMETHODIMP Next(ULONG count, FORMATETC* result, ULONG* fetched) override {
        if (!result || (!fetched && count != 1)) return E_POINTER;
        if (fetched) *fetched = 0;
        if (!count || consumed) return S_FALSE;
        *result = format; consumed = true; if (fetched) *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG count) override { if (!count) return S_OK; const bool old = consumed; consumed = true; return !old && count == 1 ? S_OK : S_FALSE; }
    IFACEMETHODIMP Reset() override { consumed = false; return S_OK; }
    IFACEMETHODIMP Clone(IEnumFORMATETC** result) override {
        if (!result) return E_POINTER;
        auto clone = Microsoft::WRL::Make<FormatEnumerator>(); clone->format = format; clone->consumed = consumed;
        return clone.CopyTo(result);
    }
};
class DataObject final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDataObject> {
public:
    FORMATETC format{};
    std::vector<uint8_t> bytes;
    IFACEMETHODIMP GetData(FORMATETC* requested, STGMEDIUM* result) override {
        if (!result) return E_POINTER;
        const HRESULT valid = QueryGetData(requested); if (FAILED(valid)) return valid;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size()); if (!memory) return E_OUTOFMEMORY;
        void* target = GlobalLock(memory); if (!target) { GlobalFree(memory); return E_OUTOFMEMORY; }
        std::memcpy(target, bytes.data(), bytes.size()); GlobalUnlock(memory);
        *result = {}; result->tymed = TYMED_HGLOBAL; result->hGlobal = memory;
        return S_OK;
    }
    IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    IFACEMETHODIMP QueryGetData(FORMATETC* value) override {
        if (!value) return E_POINTER;
        return value->cfFormat == format.cfFormat && (value->tymed & TYMED_HGLOBAL) && value->dwAspect == DVASPECT_CONTENT ? S_OK : DV_E_FORMATETC;
    }
    IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* result) override { if (result) result->ptd = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    IFACEMETHODIMP EnumFormatEtc(DWORD direction, IEnumFORMATETC** result) override {
        if (direction != DATADIR_GET) return E_NOTIMPL;
        auto values = Microsoft::WRL::Make<FormatEnumerator>(); values->format = format;
        return values.CopyTo(result);
    }
    IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};
class DropSource final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDropSource> {
public:
    IFACEMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override { return escape ? DRAGDROP_S_CANCEL : !(keys & MK_LBUTTON) ? DRAGDROP_S_DROP : S_OK; }
    IFACEMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
}
HRESULT DragShelfItem(const ShelfItem& item, const ShelfSource& source) {
    auto data = Microsoft::WRL::Make<DataObject>();
    data->bytes = source.Payload(item);
    if (data->bytes.empty()) return E_FAIL;
    const CLIPFORMAT type = item.kind == ShelfKind::Files ? CF_HDROP : item.kind == ShelfKind::Image ? CF_DIB : CF_UNICODETEXT;
    data->format = {type, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    auto drag = Microsoft::WRL::Make<DropSource>();
    DWORD effect = 0;
    return DoDragDrop(data.Get(), drag.Get(), DROPEFFECT_COPY, &effect);
}
}
