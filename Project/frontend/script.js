/* =========================================================
   LABCORE - FRONTEND CONTROLLER
   ========================================================= */

const resourceNames = {
    1: "Computer",
    2: "Printer",
    3: "File Server",
    4: "Projector",
    5: "Scanner",
    6: "Headset"
};


/* =========================================================
   PAGE NAVIGATION
   ========================================================= */

function showPage(pageId, clickedButton = null) {

    const pages = document.querySelectorAll(".page");

    pages.forEach(page => {
        page.classList.remove("active-page");
    });

    const selectedPage = document.getElementById(pageId);

    if (selectedPage) {
        selectedPage.classList.add("active-page");
    }


    const navButtons = document.querySelectorAll(".nav-button");

    navButtons.forEach(button => {
        button.classList.remove("active");
    });


    if (clickedButton) {

        clickedButton.classList.add("active");

    } else {

        navButtons.forEach(button => {

            const text = button.textContent.toLowerCase();

            if (
                (pageId === "dashboard" && text.includes("dashboard")) ||
                (pageId === "request" && text.includes("request resource")) ||
                (pageId === "status" && text.includes("student status")) ||
                (pageId === "allocations" && text.includes("allocated resources")) ||
                (pageId === "response" && text.includes("server response")) ||
                (pageId === "monitor" && text.includes("system monitor")) ||
                (pageId === "architecture" && text.includes("architecture"))
            ) {
                button.classList.add("active");
            }

        });

    }


    window.scrollTo({
        top: 0,
        behavior: "smooth"
    });
}


/* =========================================================
   QUICK ACTION NAVIGATION
   ========================================================= */

function showPageById(pageId) {
    showPage(pageId);
}


/* =========================================================
   SERVER REQUEST
   ========================================================= */

async function serverRequest(url) {

    try {

        const response = await fetch(url, {
            cache: "no-store"
        });

        const text = await response.text();

        if (!response.ok) {
            throw new Error(
                text || "Server request failed."
            );
        }

        return text;

    } catch (error) {

        console.error("Server Error:", error);

        showResponse(
            "SERVER CONNECTION ERROR\n\n" +
            error.message
        );

        updateAlert(
            "Unable to communicate with the laboratory server.",
            "error"
        );

        return null;
    }
}


/* =========================================================
   RESOURCE REQUEST
   ========================================================= */

async function requestResource() {

    const studentId =
        document.getElementById("studentId").value.trim();

    const resource =
        Number(
            document.getElementById("resource").value
        );

    const duration =
        Number(
            document.getElementById("duration").value
        );


    if (!studentId) {

        alert("Please enter Student ID.");

        return;
    }


    if (!duration || duration <= 0) {

        alert("Please enter a valid usage time.");

        return;
    }


    const resourceName =
        resourceNames[resource];


    const url =
        "/api/request" +
        "?student_id=" +
        encodeURIComponent(studentId) +
        "&resource=" +
        encodeURIComponent(resource) +
        "&duration=" +
        encodeURIComponent(duration);


    showPageById("response");


    showResponse(
        "SENDING REQUEST TO C SERVER...\n\n" +

        "Student ID : " + studentId + "\n" +
        "Resource   : " + resourceName + "\n" +
        "Duration   : " + duration + " seconds\n\n" +

        "Request is being processed by the laboratory server..."
    );


    const response =
        await serverRequest(url);


    if (response === null) {
        return;
    }


    showResponse(response);


    if (
        response.toLowerCase().includes("success") ||
        response.toLowerCase().includes("allocated") ||
        response.toLowerCase().includes("acquired")
    ) {

        updateAlert(
            resourceName +
            " request successfully processed for Student " +
            studentId + ".",
            "success"
        );

    } else if (
        response.toLowerCase().includes("error") ||
        response.toLowerCase().includes("already")
    ) {

        updateAlert(
            "Resource request could not be completed. Check the server response.",
            "error"
        );

    } else {

        updateAlert(
            "Server processed the request.",
            "info"
        );
    }


    setTimeout(() => {

        loadDashboard();

        loadAllocations();

    }, 500);
}


/* =========================================================
   DASHBOARD
   ========================================================= */

async function loadDashboard() {

    const response =
        await serverRequest(
            "/api/dashboard"
        );


    if (response === null) {
        return;
    }


    console.log(
        "Dashboard response from C server:"
    );

    console.log(response);


    updateResourceCounts(response);

    updateDashboardAlert(response);
}


/* =========================================================
   RESOURCE COUNTS
   ========================================================= */

function updateResourceCounts(text) {

    updateCount(
        text,
        "Computers",
        "computerCount"
    );

    updateCount(
        text,
        "Printers",
        "printerCount"
    );

    updateCount(
        text,
        "File Servers",
        "fileServerCount"
    );

    updateCount(
        text,
        "Projectors",
        "projectorCount"
    );

    updateCount(
        text,
        "Scanners",
        "scannerCount"
    );

    updateCount(
        text,
        "Headsets",
        "headsetCount"
    );
}


function updateCount(
    text,
    resourceName,
    elementId
) {

    const lines =
        text.split(/\r?\n/);


    for (const line of lines) {

        const cleanLine =
            line.trim();


        if (
            cleanLine
                .toLowerCase()
                .startsWith(
                    resourceName.toLowerCase()
                )
        ) {

            const match =
                cleanLine.match(
                    /:\s*(\d+)\s*\/\s*(\d+)\s*available/i
                );


            if (match) {

                const available =
                    match[1];

                const total =
                    match[2];


                const element =
                    document.getElementById(
                        elementId
                    );


                if (element) {

                    element.textContent =
                        available;

                    element.dataset.total =
                        total;
                }


                return;
            }
        }
    }


    console.warn(
        "Could not find:",
        resourceName
    );
}


/* =========================================================
   DASHBOARD ALERT
   ========================================================= */

function updateDashboardAlert(text) {

    const lines =
        text.split(/\r?\n/);

    let lowResource = null;


    for (const line of lines) {

        const match =
            line.match(
                /^(.+?):\s*(\d+)\s*\/\s*(\d+)\s*available/i
            );


        if (!match) {
            continue;
        }


        const name =
            match[1].trim();

        const available =
            Number(match[2]);

        const total =
            Number(match[3]);


        if (
            total > 0 &&
            available === 0
        ) {

            lowResource = name;

            break;
        }
    }


    if (lowResource) {

        updateAlert(
            lowResource +
            " currently has no available resources.",
            "warning"
        );

    } else {

        updateAlert(
            "All laboratory resources are being monitored normally.",
            "success"
        );
    }
}


/* =========================================================
   ALERT PANEL
   ========================================================= */

function updateAlert(message, type = "info") {

    const messageElement =
        document.getElementById(
            "alertMessage"
        );

    const panel =
        document.getElementById(
            "alertPanel"
        );


    if (!messageElement || !panel) {
        return;
    }


    messageElement.textContent =
        message;


    panel.dataset.alertType =
        type;
}


/* =========================================================
   STUDENT STATUS
   ========================================================= */

async function viewStudentStatus() {

    const studentId =
        document
            .getElementById(
                "statusStudentId"
            )
            .value
            .trim();


    if (!studentId) {

        alert(
            "Please enter Student ID."
        );

        return;
    }


    showPageById("response");


    showResponse(
        "REQUESTING STUDENT STATUS...\n\n" +

        "Student ID: " +
        studentId
    );


    const url =
        "/api/status?student_id=" +
        encodeURIComponent(
            studentId
        );


    const response =
        await serverRequest(url);


    if (response === null) {
        return;
    }


    showResponse(response);
}


/* =========================================================
   ALLOCATIONS
   ========================================================= */

async function loadAllocations() {

    const response =
        await serverRequest(
            "/api/allocations"
        );


    if (response === null) {
        return;
    }


    const element =
        document.getElementById(
            "allocationResult"
        );


    if (element) {

        element.textContent =
            response;
    }
}


/* =========================================================
   SERVER RESPONSE
   ========================================================= */

function showResponse(message) {

    const element =
        document.getElementById(
            "serverResponse"
        );


    if (element) {

        element.textContent =
            message;
    }
}


/* =========================================================
   DARK / LIGHT MODE
   ========================================================= */

function toggleTheme() {

    document.body.classList.toggle(
        "dark-mode"
    );


    const button =
        document.getElementById(
            "themeToggle"
        );


    const darkMode =
        document.body.classList.contains(
            "dark-mode"
        );


    if (darkMode) {

        button.textContent = "☀️";

        localStorage.setItem(
            "labcore-theme",
            "dark"
        );

    } else {

        button.textContent = "🌙";

        localStorage.setItem(
            "labcore-theme",
            "light"
        );
    }
}


/* =========================================================
   LOAD SAVED THEME
   ========================================================= */

function loadSavedTheme() {

    const savedTheme =
        localStorage.getItem(
            "labcore-theme"
        );


    if (savedTheme === "dark") {

        document.body.classList.add(
            "dark-mode"
        );


        const button =
            document.getElementById(
                "themeToggle"
            );


        if (button) {
            button.textContent = "☀️";
        }
    }
}


/* =========================================================
   INITIALIZATION
   ========================================================= */

window.addEventListener(
    "load",
    function () {

        loadSavedTheme();

        loadDashboard();

        loadAllocations();

    }
);
