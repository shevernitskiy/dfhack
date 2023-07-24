local _ENV = mkmodule('plugins.dfint')

local overlay = require('plugins.overlay')

DfintOverlay = defclass(DfintOverlay, overlay.OverlayWidget)
DfintOverlay.ATTRS{
    viewscreens='all',
    default_enabled=true,
    overlay_only=true,
}

function DfintOverlay:onRenderFrame(dc)
    renderOverlay()
end

OVERLAY_WIDGETS = {dfint=DfintOverlay}

return _ENV