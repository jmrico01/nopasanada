"use strict";

// const IMAGE_BASE_URL = "https://nopasanada.s3.amazonaws.com";
const IMAGE_BASE_URL = ".";

const FEATURED_IMAGE_FADE_MS = 400;
const IMAGE_ANIM_MS = 250;

const HOMEPAGE_CATEGORY = "home";

let allEntries_ = null;
let featuredEntries_ = null;
let loadedEntries_ = null; // TODO revisit name + use-case, will need more groups of stuff

let featuredImages_ = null;
let posterImages_ = null;

let posterTemplate_ = null;
let postersPerScreen_ = 5;
let posterPositionIndex_ = 0;

let prevCategory_ = null;
let imgCycleInterval_ = null;
let allImagesLoaded_ = false;

function GetCurrentCategory()
{
    if (featuredEntries_ !== null) {
        let hash = window.location.hash;
        let hashIndex = hash.indexOf("#");
        if (hashIndex !== -1) {
            let hashCategory = hash.substring(hashIndex + 1, hash.length);
            if (featuredEntries_.hasOwnProperty(hashCategory) !== -1) {
                return hashCategory;
            }
        }
    }

    return HOMEPAGE_CATEGORY;
}

function AreImagesLoaded(category, featuredIndex)
{
    if (featuredIndex >= featuredImages_[category].length) {
        console.error("AreImagesLoaded: featuredIndex " + featuredIndex
            + " out of bounds for category " + category);
        return false;
    }

    const imageSet = featuredImages_[category][featuredIndex];
    if (imageSet.length === 0) {
        return false;
    }
    for (let i = 0; i < imageSet.length; i++) {
        if (!imageSet[i].hasOwnProperty("loaded") || !imageSet[i].loaded) {
            return false;
        }
    }
    return true;
}

function SetFeaturedInfo(entry)
{
    $("#featuredPretitle").html(entry.pretitle);
    $("#featuredTitle a").html(entry.title);
    $("#featuredTitle a").attr("href", entry.uri);
    $("#featuredDecoration").html(entry.decoration);
    $("#featuredText1").html(entry.text1);
    $("#featuredText2").html(entry.text2);
    $("#header a").unbind("mouseover mouseout");
    $("#header a").mouseover(function() {
        $(this).css("color", entry.highlightColor)
    });
    $("#header a").mouseout(function() {
        $(this).css("color", "#ffffff");
    });
}

function SetFeaturedImageSet(imageSet)
{
    /*
    if (imgCycleInterval_ !== null) {
        clearInterval(imgCycleInterval_);
        imgCycleInterval_ = null;
    }

    let imageClass = ".featuredImage-" + category;
    let imageId = "#featuredImage-" + category + "-0";
    let $currentActive = $("#landingImageCycler img.active");
    let $currentTransition = $("#landingImageCycler img.transition");
    let $featuredImage = $(imageId);
    if (instant) {
        $currentActive.removeClass("active");
        $currentTransition.removeClass("transition");
        $featuredImage.addClass("active");
    }
    else {
        if ($currentActive.length === 0 && $currentTransition.length === 0) {
            $featuredImage.addClass("active");
        }
        else if ($currentActive.length > 0 && $currentTransition.length === 0) {
            $featuredImage.addClass("transition");
            $currentActive.fadeOut(FEATURED_IMAGE_FADE_MS, function() {
                $currentActive.removeClass("active").show();
                $(".transition").addClass("active").removeClass("transition");
            });
        }
        else if ($currentActive.length === 0 && $currentTransition.length > 0) {
            // strange case, small timing issue, but this is JS so who knows
        }
        else if ($currentActive.length > 0 && $currentTransition.length > 0) {
            $(".transition").removeClass("transition");
            $featuredImage.addClass("transition");
        }
    }

    let counter = 0;
    let counterDir = 1;
    imgCycleInterval_ = setInterval(function() {
        let numImages = featuredEntries[category].images.length;
        if (numImages === 1) {
            return;
        }
        let imageId = "#featuredImage-" + category + "-" + counter;
        let $currentActive = $("#landingImageCycler img.active");
        $currentActive.removeClass("active");
        let $featuredImage = $(imageId).addClass("active");
        if (counter >= numImages - 1) {
            counterDir = -1;
        }
        else if (counter <= 0) {
            counterDir = 1;
        }
        counter += counterDir;
    }, IMAGE_ANIM_MS);
    */
}

function MovePosters(entries, indexDelta)
{
    let indexMax = Math.floor((entries.length - 1) / postersPerScreen_);
    posterPositionIndex_ = Math.min(Math.max(posterPositionIndex_ + indexDelta, 0), indexMax);

    $("#contentList").css("margin-left", -posterPositionIndex_ * window.innerWidth);
    if (posterPositionIndex_ == 0) {
        $("#contentArrowLeftButton").hide();
    }
    else {
        $("#contentArrowLeftButton").show();
    }
    if (posterPositionIndex_ == indexMax) {
        $("#contentArrowRightButton").hide();
    }
    else {
        $("#contentArrowRightButton").show();
    }
}

function SetPosterContentWidth(entries)
{
    let width = Math.ceil(entries.length / postersPerScreen_) * window.innerWidth;
    $("#contentList").css("width", width);
}

function ResetPosters(entries)
{
    SetPosterContentWidth(entries);
    let $contentList = $("#contentList");
    $contentList.html("");

    $contentList.append("<div class=\"entrySpaceEdge\"></div>");
    for (let i = 0; i < entries.length; i++) {
        let entryData = entries[i];

        let $entry = $(posterTemplate_);
        $entry.find("a").attr("href", entryData.uri);
        $entry.find("img").attr("src", IMAGE_BASE_URL + entryData.image);
        $entry.find(".entryNumber").html(i + 1 + ".");
        $entry.find(".entryText").html(entryData.title);
        $contentList.append($entry);

        if (i !== entries.length - 1) {
            if ((i + 1) % postersPerScreen_ === 0) {
                $contentList.append("<div class=\"entrySpaceEdge\"></div>");
                if (isNarrow_) {
                    $contentList.append("<div style=\"width: 100%; height: 65vw;\"></div>");
                }
                $contentList.append("<div class=\"entrySpaceEdge\"></div>");
            }
            else {
                $contentList.append("<div class=\"entrySpace\"></div>");
            }
        }
    }

    posterPositionIndex_ = 0;
    MovePosters(entries, 0);
}

function HandleScroll()
{
    let headerOpacity = Math.min(document.documentElement.scrollTop / window.innerHeight, 1.0);
    $("#header").css("background-color", "rgba(0%, 0%, 0%, " + headerOpacity * 100.0 + "%)");
}

function OnAspectChanged(narrow)
{
    if (narrow) {
        cssNarrow_.href = "css/main-narrow.css";
        postersPerScreen_ = 3;
        $("#contentList").css("width", "100%");
    }
    else {
        cssNarrow_.href = "";
        postersPerScreen_ = 5;
    }

    if (loadedEntries_ !== null) {
        ResetPosters(loadedEntries_);
    }
}

function OnResize()
{
    if (isNarrow_) {
        $("#screenPosters").css("height", "auto");
    }

    let aspect = window.innerWidth / window.innerHeight;
    if (allImagesLoaded_) {
        $(".featuredImage").each(function(index) {
            let $this = $(this);
            var img = new Image();
            img.onload = function() {
                let imageAspect = img.width / img.height;
                if (aspect > imageAspect) {
                    $this.width("100%");
                    $this.height("auto");
                }
                else {
                    $this.width("auto");
                    $this.height("100%");
                    let imageWidth = document.documentElement.clientHeight * imageAspect;
                    let marginX = (imageWidth - document.documentElement.clientWidth) / 2;
                    $this.css("margin-left", -marginX);
                }
            };
            img.src = $this.attr("src");
        });
    }

    if (loadedEntries_ !== null) {
        SetPosterContentWidth(loadedEntries_);
    }
}

window.onscroll = HandleScroll;

function OnHashChanged()
{
    let category = GetCurrentCategory();
    if (category !== prevCategory_) {
        prevCategory_ = category;
        SetFeaturedInfo(featuredEntries_[category][0]);

        loadedEntries_ = [];
        for (let i = 0; i < allEntries_.length; i++) {
            if (allEntries_[i].tags.indexOf(category) !== -1) {
                loadedEntries_.push(allEntries_[i]);
            }
        }
        ResetPosters(loadedEntries_);
    }
}

function LoadNextFeaturedImageSet(category, callback)
{
    let nextFeaturedIndex = null;
    for (let i = 0; i < featuredImages_[category].length; i++) {
        if (featuredImages_[category][i].length === 0) {
            nextFeaturedIndex = i;
            break;
        }
    }
    if (nextFeaturedIndex === null) {
        return false;
    }

    const nextImageSet = featuredEntries_[category][nextFeaturedIndex].images;
    const imagesToLoad = nextImageSet.length;
    let imagesLoaded = 0;
    for (let i = 0; i < nextImageSet.length; i++) {
        let imgUrl = IMAGE_BASE_URL + nextImageSet[i];
        let img = new Image;
        img.onload = function() {
            featuredImages_[category][nextFeaturedIndex][i].loaded = true;
            imagesLoaded += 1;
            if (imagesLoaded >= imagesToLoad) {
                callback();
            }
        };
        img.src = imgUrl;
        featuredImages_[category][nextFeaturedIndex].push(img);
    }

    return true;
}

function OnFeaturedEntriesLoaded(featured)
{
    featuredEntries_ = {};
    featuredImages_ = {};
    for (let category in featured) {
        featuredEntries_[category] = [];
        featuredImages_[category] = [];
        for (let i = 0; i < featured[category].length; i++) {
            const uri = featured[category][i];
            for (let j = 0; j < allEntries_.length; j++) {
                if (allEntries_[j].uri === uri) {
                    let entry = allEntries_[j].featuredInfo;
                    entry.uri = uri;
                    featuredEntries_[category].push(entry);
                    featuredImages_[category].push([]);
                    break;
                }
            }
        }
    }

    console.log(AreImagesLoaded(HOMEPAGE_CATEGORY, 0));
    SetFeaturedInfo(featuredEntries_[HOMEPAGE_CATEGORY][0]);
    LoadNextFeaturedImageSet(HOMEPAGE_CATEGORY, function() {
        console.log(AreImagesLoaded(HOMEPAGE_CATEGORY, 0));
        console.log(featuredImages_);
    });
}

window.onhashchange = OnHashChanged;

$(document).ready(function() {
    posterTemplate_ = $("#posterTemplate").html();
    $("#posterTemplate").remove();

    let entriesLoaded = false;
    let featured = null;

    $.ajax({
        type: "GET",
        url: "/entries",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            allEntries_ = data;
            loadedEntries_ = [];
            let currentCategory = GetCurrentCategory();
            for (let i = 0; i < allEntries_.length; i++) {
                allEntries_[i].title = allEntries_[i].title.toUpperCase();
                let entryTags = allEntries_[i].tags;
                for (let j = 0; j < entryTags.length; j++) {
                    if (entryTags[j] === currentCategory) {
                        loadedEntries_.push(allEntries_[i]);
                    }
                }
            }

            // TODO prioritize featured image loading over these
            ResetPosters(loadedEntries_);

            if (featured !== null) {
                OnFeaturedEntriesLoaded(featured);
            }
            entriesLoaded = true;
        },
        error: function(error) {
            console.error(error);
        }
    });

    $.ajax({
        type: "GET",
        url: "/featured",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            if (entriesLoaded) {
                OnFeaturedEntriesLoaded(data);
            }
            featured = data;
        },
        error: function(error) {
            console.error(error);
        }
    });

    $("#contentArrowLeftButton").on("click", function() {
        MovePosters(loadedEntries_, -1);
    })
    $("#contentArrowRightButton").on("click", function() {
        MovePosters(loadedEntries_, 1);
    })

    OnResize();
    HandleScroll();

    $("#content").css("visibility", "visible");
});
