const FEATURED_TAG_PREFIX = "featured-";
const TABLE_FIELDS = [
    [ "date" , "Date"     ],
    [ "type" , "Type"     ],
    [ "title", "Title"    ],
    [ "tags" , "Tags"     ],
    [ "link" , "ID / URL" ],
];

let resetInProgress_  = false;
let commitInProgress_ = false;
let deployInProgress_ = false;

let entryData_ = null;

// TODO duplicated in server.js
function GetDateStringsFromUnixTime(unixTime)
{
    let date = new Date(unixTime);
    let dayString = date.getDate().toString();
    if (dayString.length < 2) {
        dayString = "0" + dayString;
    }
    else if (dayString.length > 2) {
        throw new Error("day string on date has length > 2");
    }
    let monthString = (date.getMonth() + 1).toString();
    if (monthString.length < 2) {
        monthString = "0" + monthString;
    }
    else if (monthString.length > 2) {
        throw new Error("month string on date has length > 2");
    }
    return {
        day: dayString,
        month: monthString,
        year: date.getFullYear().toString()
    };
}

function FormatTableFieldValue(tableField, entry)
{
    const entryValue = entry[tableField[0]];
    if (tableField[0] === "link") {
        return "<a href=\"/entry?entry=" + entryValue + "\">" + entryValue + "</a>";
    }
    else if (tableField[0] === "title") {
        return "<i>" + entryValue + "</i>";
    }
    else if (tableField[0] === "tags") {
        return entryValue.join(" ");
    }
    return entryValue;
}

function SaveFeatured()
{
    console.log("saving featured");

    let featured = {};
    $("#featuredForm select").each(function() {
        let $this = $(this);
        featured[$this.attr("name")] = $this.val();
    });

    $.ajax({
        type: "POST",
        url: "/featured",
        contentType: "application/json",
        async: true,
        data: featured,
        dataType: "text",
        success: function(data) {
            $("#statusMessage").html("New entry created successfully.");
        },
        error: function(error) {
            console.log(error);
            $("#statusMessage").html("New entry creation failed, error: " + error.responseText);
        }
    });
}

function UpdateFeaturedTable(featured)
{
    let featuredTableHtml = "<form id=\"featuredForm\"><table><tr><th>Tag</th><th>ID / URL</th></tr>";
    for (let category in featured) {
        let uriHtml = "<select name=\"" + category + "\">";
        for (let i = 0; i < entryData_.length; i++) {
            uriHtml += "<option value=\"" + entryData_[i].link + "\">" + entryData_[i].link + "</option>";
        }
        featuredTableHtml += "<tr><td>" + category + "</td><td>" + uriHtml + "</td></tr>\n";
    }
    featuredTableHtml += "</table></form>";
    $("#featuredEntryTable").html(featuredTableHtml);

    for (let category in featured) {
        let $selectCategory = $("select[name=" + category + "]");
        $selectCategory.val(featured[category]);
        $selectCategory.change(function() {
            if (featuredRefreshInProgress_) {
                return;
            }
            featuredRefreshInProgress_ = true;
            $("#statusMessage").html("Saving featured entries...");

            let newFeatured = {};
            $("#featuredForm select").each(function() {
                let $this = $(this);
                newFeatured[$this.attr("name")] = $this.val();
            });

            $.ajax({
                type: "POST",
                url: "/featured",
                contentType: "application/json",
                async: true,
                data: JSON.stringify(newFeatured),
                dataType: "text",
                success: function(data) {
                    $("#statusMessage").html("Saved featured entries.");
                    featuredRefreshInProgress_ = false;
                },
                error: function(error) {
                    console.log(error);
                    $("#statusMessage").html("Failed to save featured entries, error: " + error.responseText);
                    featuredRefreshInProgress_ = false;
                }
            });

        });
    }
}

$(document).ready(function() {
    $(".modal").hide();
    $(".modal").click(function(event) {
        if (event.target === this) {
            $(".modal").hide();
        }
    });

    $.get("/entries", function(data) {
        entryData_ = data;
        let tableHtml = "<tr>";
        for (let j = 0; j < TABLE_FIELDS.length; j++) {
            if (TABLE_FIELDS[j][0] === "date") {
                tableHtml += "<th style=\"width: 64pt;\">";
            }
            else {
                tableHtml += "<th>";
            }
            tableHtml += TABLE_FIELDS[j][1];
            tableHtml += "</th>";
        }
        tableHtml += "</tr>";

        for (let i = 0; i < data.length; i++) {
            const entry = data[i];
            tableHtml += "<tr>";
            for (let j = 0; j < TABLE_FIELDS.length; j++) {
                tableHtml += "<td>";
                tableHtml += FormatTableFieldValue(TABLE_FIELDS[j], data[i]);
                tableHtml += "</td>";
            }
            tableHtml += "</tr>";
        }

        $("#entryTable").html(tableHtml);

        $.get("/featured", function(data) {
            UpdateFeaturedTable(data);
            featuredRefreshInProgress_ = false;
        });
    });

    $.ajax({
        type: "GET",
        url: "/previewSite",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            let previewUrl = data.url;
            $("#previewLink").attr("href", previewUrl);
        },
        error: function(error) {
            console.error(error);
        }
    });

    $("#newEntryButton").click(function() {
        let newEntryHtml = "<h1>New Entry</h1>";
        newEntryHtml += "<form id=\"newEntryForm\">";
        // URI (name)
        let dateStrings = GetDateStringsFromUnixTime(Date.now());
        newEntryHtml += "<h3>URL</h3>/content/" + dateStrings.year + dateStrings.month
            + "/ <input type=\"text\" name=\"uniqueName\"></input><br>";
        // Copy from
        newEntryHtml += "<h3>Copy from</h3><select name=\"copyFrom\"><option value=\"none\">None</option>";
        for (let i = 0; i < entryData_.length; i++) {
            newEntryHtml += "<option value=\"" + entryData_[i].link + "\">" + entryData_[i].link + "</option>";
        }
        newEntryHtml += "</select><br>";
        // Content type
        newEntryHtml += "<h3>or Type (if \"Copy from: None\")</h3><select name=\"contentType\"><option value=\"article\">article</option><option value=\"newsletter\">newsletter</option><option value=\"text\">text</option><option value=\"video\">video</option></select><br>";
        // Submit
        newEntryHtml += "<input type=\"submit\" value=\"Create\"></input>";
        newEntryHtml += "</form>";
        $(".modal-content").html(newEntryHtml);
        $(".modal").show();
        $("#newEntryForm").submit(function(event) {
            event.preventDefault();

            let $form = $("#newEntryForm");
            let uniqueName = $form.find("input[name=uniqueName]").val();
            let uniqueNameRegex = /^[a-z0-9\-]+$/g;
            if (!uniqueNameRegex.test(uniqueName)) {
                $("#statusMessage").html("Entry name should only have lower-case letters, numbers, or dashes.");
                return;
            }
            let copyFrom = $form.find("select[name=copyFrom]").val();
            if (copyFrom === "none") {
                copyFrom = null;
            }
            let formData = {
                uniqueName: uniqueName,
                contentType: $form.find("select[name=contentType]").val(),
                copyFrom: copyFrom
            };
            $.ajax({
                type: "POST",
                url: "/newEntry",
                contentType: "application/json",
                async: true,
                data: JSON.stringify(formData),
                dataType: "text",
                success: function(data) {
                    $("#statusMessage").html("New entry created successfully.");
                    location.reload(true);
                },
                error: function(error) {
                    console.log(error);
                    $("#statusMessage").html("New entry creation failed, error: " + error.responseText);
                }
            });
            $(".modal").hide();
        });
    });

    $("#diffButton").click(function() {
        $.get("/diff", function(data) {
            let diffHtml = "<h1>DIFF</h1>";
            for (let i = 0; i < data.length; i++) {
                diffHtml += "<p>" + data[i].flag + " " + data[i].file + "</p>";
            }
            $(".modal").show();
            $(".modal-content").html(diffHtml);
        });
    });

    $("#resetButton").click(function() {
        if (!resetInProgress_) {
            resetInProgress_ = true;
            $("#statusMessage").html("Pulling and resetting...");
            $.ajax({
                type: "POST",
                url: "/reset",
                contentType: "application/text",
                dataType: "text",
                async: true,
                data: "",
                success: function(data) {
                    window.location = '/';
                    resetInProgress_ = false;
                },
                error: function(error) {
                    window.location = '/';
                    resetInProgress_ = false;
                }
            });
        }
    });

    $("#commitButton").click(function() {
        if (!commitInProgress_) {
            commitInProgress_ = true;
            $("#statusMessage").html("Committing changes...");
            $.ajax({
                type: "POST",
                url: "/commit",
                contentType: "application/text",
                dataType: "text",
                async: true,
                data: "",
                success: function(data) {
                    $("#statusMessage").html("Commit successful.");
                    commitInProgress_ = false;
                },
                error: function(error) {
                    console.log(error);
                    $("#statusMessage").html("Commit failed, error: " + error.responseText);
                    commitInProgress_ = false;
                }
            });
        }
    });

    $("#deployButton").click(function() {
        if (!deployInProgress_) {
            deployInProgress_ = true;
            $("#statusMessage").html("Deploying changes...");
            $.ajax({
                type: "POST",
                url: "/deploy",
                contentType: "application/text",
                dataType: "text",
                async: true,
                data: "",
                success: function(data) {
                    $("#statusMessage").html("Deploy successful.");
                    deployInProgress_ = false;
                },
                error: function(error) {
                    console.log(error);
                    $("#statusMessage").html("Deploy failed, error: " + error.responseText);
                    deployInProgress_ = false;
                }
            });
        }
    });
});
