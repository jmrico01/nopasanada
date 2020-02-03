"use strict";

function OnAspectChanged(narrow)
{
    if (narrow) {
        cssNarrow_.href = "../../../css/entry-narrow.css";
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
    // TODO Duplicated in entry scripts
    $(".headerSubcategories").hide();
    $(".headerCategory").hover(function() {
        $(this).find(".headerSubcategories").show();
    }, function() {
        $(this).find(".headerSubcategories").hide();
    });

    $("#content").css("visibility", "visible");
});
