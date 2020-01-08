"use strict";

function OnAspectChanged(narrow)
{
    if (narrow) {
        cssNarrow_.href = "../../../css/article-narrow.css";
    }
    else {
        cssNarrow_.href = "";
    }
}

function OnResize()
{
    // Called from resize.js
}

$(document).ready(function() {
    $("#content").css("visibility", "visible");
});
