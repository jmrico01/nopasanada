"use strict";

function OnAspectChanged(narrow)
{
    if (narrow) {
        cssNarrow_.href = "../../../css/article-narrow.css";
        $("#headerDesktop").hide();
        $("#headerMobile").show();
    }
    else {
        cssNarrow_.href = "";
        $("#headerMobile").hide();
        $("#headerDesktop").show();
    }
}

function OnResize()
{
    // Called from resize.js
}

function OnHashChanged()
{
    $(".articleSubcontainer").hide();

    let hash = window.location.hash;
    let target = "#articleSubcontainer";
    if (hash === "#1") {
        target += "1";
    }
    else if (hash === "#2") {
        target += "2";
    }
    else if (hash === "#3") {
        target += "3";
    }
    else if (hash === "#4") {
        target += "4";
    }
    else {
        target += "1";
    }

    $(target).show();
    if (hash === "") {
        window.scrollTo(0, 0);
    }
    else {
        let topOffset = $("#header").height();
        $([document.documentElement, document.body]).animate({
            scrollTop: $("#articleContainer").offset().top - topOffset
        }, 200);
    }
}

window.onhashchange = OnHashChanged;

$(document).ready(function() {
    if ($(".articleAudio").attr("src") === "") {
        $(".articleAudio").remove();
    }
    OnHashChanged();
    $("#content").css("visibility", "visible");
});
