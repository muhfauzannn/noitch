#pragma once
#include <ole2.h>
#include <wrl.h>
#include <functional>
#include "shelf_source.h"

namespace noitch {
class ShelfDropTarget final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDropTarget> {
public:
    bool enabled = true;
    std::function<void(bool)> targeting;
    std::function<bool(std::vector<std::string>)> dropped;
    IFACEMETHODIMP DragEnter(IDataObject* object, DWORD, POINTL, DWORD* effect) override {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        accepts_ = enabled && object && SUCCEEDED(object->QueryGetData(&format));
        *effect = accepts_ && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (accepts_ && targeting) targeting(true);
        return S_OK;
    }
    IFACEMETHODIMP DragOver(DWORD, POINTL, DWORD* effect) override { *effect = accepts_ && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE; return S_OK; }
    IFACEMETHODIMP DragLeave() override { accepts_ = false; if (targeting) targeting(false); return S_OK; }
    IFACEMETHODIMP Drop(IDataObject* object, DWORD, POINTL, DWORD* effect) override {
        *effect = DROPEFFECT_NONE;
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (accepts_ && object && SUCCEEDED(object->GetData(&format, &medium))) {
            if (medium.tymed == TYMED_HGLOBAL) {
                auto paths = ShelfSource::PathsFromDrop(static_cast<HDROP>(medium.hGlobal));
                if (!paths.empty() && dropped && dropped(std::move(paths))) *effect = DROPEFFECT_COPY;
            }
            ReleaseStgMedium(&medium);
        }
        return DragLeave();
    }
private:
    bool accepts_ = false;
};
HRESULT DragShelfItem(const ShelfItem& item, const ShelfSource& source);
}
