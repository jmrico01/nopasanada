"use strict";

// All pages that include this script must have the following functions defined:
//     function OnAspectChanged(isNarrow) { ... }
//     function OnResize() { ... }

let cssNarrow_ = null;
let cssTextNarrow_ = null;
let isNarrow_ = null;

let prevHeight_ = null;

function OnResizeBase()
{
    const TRANSITION_ASPECT = 1.45;
    let aspectChanged = false;

    let aspect = window.innerWidth / window.innerHeight;
    let isNarrow = aspect < TRANSITION_ASPECT;
    if (isNarrow !== isNarrow_) {
        isNarrow_ = isNarrow;
        aspectChanged = true;
    }

    if (!isNarrow || window.innerHeight != prevHeight_) {
        let headerHeight = $("#header").height();
        $(".screen").each(function(index) {
            let $this = $(this);
            $this.height(window.innerHeight - headerHeight);
            $this.css("padding-top", headerHeight);
            $this.css("padding-bottom", 0);
        });
    }

    if (aspectChanged) {
        if (isNarrow) {
            cssTextNarrow_.href = "../../../css/text-narrow.css";
        }
        else {
            cssTextNarrow_.href = "";
        }

        OnAspectChanged(isNarrow);
    }

    OnResize();
}

$(document).ready(function() {
    cssTextNarrow_ = document.createElement("link");
    cssTextNarrow_.rel = "stylesheet";
    cssTextNarrow_.type = "text/css";
    document.getElementsByTagName("head")[0].appendChild(cssTextNarrow_);

    cssNarrow_ = document.createElement("link");
    cssNarrow_.rel = "stylesheet";
    cssNarrow_.type = "text/css";
    document.getElementsByTagName("head")[0].appendChild(cssNarrow_);

    $(window).resize(OnResizeBase);
    OnResizeBase();
});