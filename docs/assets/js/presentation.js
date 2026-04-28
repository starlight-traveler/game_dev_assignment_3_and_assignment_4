(function () {
  function ready(callback) {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", callback);
      return;
    }
    callback();
  }

  ready(function () {
    var main = document.querySelector("main.content-card");
    var controls = document.querySelector("[data-presentation-controls]");
    if (!main || !controls) {
      return;
    }

    var headings = Array.prototype.slice.call(main.querySelectorAll("h2[id]"));
    if (!headings.length) {
      return;
    }

    var slides = headings.map(function (heading) {
      var section = document.createElement("section");
      section.className = "presentation-slide";
      section.setAttribute("data-slide-id", heading.id);
      heading.parentNode.insertBefore(section, heading);
      section.appendChild(heading);

      var next = section.nextSibling;
      while (next && !(next.nodeType === 1 && next.tagName === "H2")) {
        var after = next.nextSibling;
        section.appendChild(next);
        next = after;
      }

      return section;
    });

    var previousButton = controls.querySelector("[data-prev-slide]");
    var nextButton = controls.querySelector("[data-next-slide]");
    var detailsButton = controls.querySelector("[data-toggle-details]");
    var allButton = controls.querySelector("[data-toggle-all]");
    var status = controls.querySelector("[data-slide-status]");
    var tabLinks = Array.prototype.slice.call(document.querySelectorAll(".presentation-tabs a"));
    var currentIndex = 0;
    var showAll = false;
    var detailsOpen = false;

    function titleFor(slide) {
      var heading = slide.querySelector("h2");
      return heading ? heading.textContent : "Slide";
    }

    function updateTabs() {
      tabLinks.forEach(function (link) {
        var targetId = link.getAttribute("href").slice(1);
        var isActive = slides[currentIndex] &&
          slides[currentIndex].getAttribute("data-slide-id") === targetId;
        link.classList.toggle("is-active", isActive && !showAll);
        if (isActive && !showAll) {
          link.setAttribute("aria-current", "step");
        } else {
          link.removeAttribute("aria-current");
        }
      });
    }

    function setDetails(open) {
      detailsOpen = open;
      var scope = showAll ? main : slides[currentIndex];
      Array.prototype.slice.call(scope.querySelectorAll("details.speaker-notes")).forEach(function (details) {
        details.open = detailsOpen;
      });
      detailsButton.textContent = detailsOpen ? "Collapse Details" : "Expand Details";
    }

    function render() {
      slides.forEach(function (slide, index) {
        var isVisible = showAll || index === currentIndex;
        slide.hidden = !isVisible;
        slide.classList.toggle("is-active", isVisible && !showAll);
      });

      previousButton.disabled = showAll || currentIndex === 0;
      nextButton.disabled = showAll || currentIndex === slides.length - 1;
      allButton.textContent = showAll ? "Slide Mode" : "Show All";

      if (showAll) {
        status.textContent = slides.length + " slides visible";
      } else {
        status.textContent = "Slide " + (currentIndex + 1) + " of " + slides.length +
          ": " + titleFor(slides[currentIndex]);
      }

      updateTabs();
    }

    function goTo(index, updateHash) {
      currentIndex = Math.max(0, Math.min(slides.length - 1, index));
      showAll = false;
      render();
      if (detailsOpen) {
        setDetails(true);
      }
      if (updateHash) {
        var id = slides[currentIndex].getAttribute("data-slide-id");
        try {
          history.replaceState(null, "", "#" + id);
        } catch (error) {
          window.location.hash = id;
        }
      }
      controls.scrollIntoView({ block: "start" });
    }

    function findSlideIndexById(id) {
      for (var i = 0; i < slides.length; ++i) {
        if (slides[i].getAttribute("data-slide-id") === id) {
          return i;
        }
      }
      return -1;
    }

    previousButton.addEventListener("click", function () {
      goTo(currentIndex - 1, true);
    });

    nextButton.addEventListener("click", function () {
      goTo(currentIndex + 1, true);
    });

    detailsButton.addEventListener("click", function () {
      setDetails(!detailsOpen);
    });

    allButton.addEventListener("click", function () {
      showAll = !showAll;
      render();
      if (detailsOpen) {
        setDetails(true);
      }
      controls.scrollIntoView({ block: "start" });
    });

    tabLinks.forEach(function (link) {
      link.addEventListener("click", function (event) {
        var id = link.getAttribute("href").slice(1);
        var index = findSlideIndexById(id);
        if (index < 0) {
          return;
        }
        event.preventDefault();
        goTo(index, true);
      });
    });

    document.addEventListener("keydown", function (event) {
      var tag = event.target.tagName;
      if (tag === "INPUT" ||
          tag === "TEXTAREA" ||
          tag === "SELECT" ||
          tag === "BUTTON" ||
          tag === "A" ||
          tag === "SUMMARY" ||
          event.target.isContentEditable) {
        return;
      }

      if (event.key === "ArrowRight" || event.key === "PageDown" || event.key === " ") {
        event.preventDefault();
        goTo(currentIndex + 1, true);
      } else if (event.key === "ArrowLeft" || event.key === "PageUp") {
        event.preventDefault();
        goTo(currentIndex - 1, true);
      } else if (event.key === "Home") {
        event.preventDefault();
        goTo(0, true);
      } else if (event.key === "End") {
        event.preventDefault();
        goTo(slides.length - 1, true);
      } else if (event.key.toLowerCase() === "d") {
        event.preventDefault();
        setDetails(!detailsOpen);
      } else if (event.key.toLowerCase() === "a") {
        event.preventDefault();
        showAll = !showAll;
        render();
        if (detailsOpen) {
          setDetails(true);
        }
      }
    });

    var initialIndex = window.location.hash ?
      findSlideIndexById(window.location.hash.slice(1)) :
      0;
    if (initialIndex >= 0) {
      currentIndex = initialIndex;
    }

    controls.classList.add("is-ready");
    setDetails(false);
    render();
  });
}());
