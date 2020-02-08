"use strict";

// All pages that include this script must have the following functions defined:
//     function OnCategoriesLoaded(categories) { ... }
// 
// And the following variables already defined:
//     const BASE_URL = "the base url (e.g. ../../.. for entries)";
// 

function GenerateHeader(categories)
{
    // TODO replace links
    let headerHtml = "<h1 id=\"headerLogo\"><a href=\"" + BASE_URL + "/#\">NO PASA NADA.</a></h1>";
    headerHtml += "<div id=\"headerCategories\">";
    let displayOrder = categories.displayOrder;
    for (let i = 0; i < displayOrder.length; i++) {
        let category = displayOrder[i];
        let categoryInfo = categories[category];
        headerHtml += "<div class=\"headerCategory\"><a href=\"" + BASE_URL + "/#" + category + "\">";
        headerHtml += "<h2>" + categoryInfo.name.toUpperCase() + "</h2></a>";
        if ("displayOrder" in categoryInfo) {
            headerHtml += "<div class=\"headerSubcategories\">";
            for (let j = 0; j < categoryInfo.displayOrder.length; j++) {
                let subcategory = categoryInfo.displayOrder[j];
                if (!(subcategory in categoryInfo)) {
                    console.error(category + " missing display order subcategory " + subcategory);
                }
                let subcategoryInfo = categoryInfo[subcategory];
                headerHtml += "<div class=\"headerSubcategory\"><a href=\"" + BASE_URL + "/#" + category + "-" + subcategory + "\">";
                headerHtml += "<h3>" + subcategoryInfo.name.toUpperCase() + "</h3></a></div>";
            }
            headerHtml += "</div>";
        }
        headerHtml += "</div>";
    }
    headerHtml += "</div>";

    $("#header").html(headerHtml);
}

$(document).ready(function() {
    $.ajax({
        type: "GET",
        url: "/categories",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            let categoriesCopy = JSON.parse(JSON.stringify(data));

            GenerateHeader(data);
            //$(".headerSubcategories").css("visibility", "visible");
            $(".headerSubcategories").hide();
            $(".headerCategory").hover(function() {
                $(this).find(".headerSubcategories").show();
            }, function() {
                $(this).find(".headerSubcategories").hide();
            });

            OnCategoriesLoaded(categoriesCopy);
        },
        error: function(error) {
            console.error(error);
        }
    });
});