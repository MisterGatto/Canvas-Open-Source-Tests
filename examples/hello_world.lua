-- =========================================================
-- @name: Hello World Mod
-- @author: Canvas Team
-- @version: 1.0.0
-- @description: Starter Hello World script demonstrating GameGuardian (gg) and Canvas APIs.
-- =========================================================

-- Display an initial greeting toast on startup
gg.toast("Hello World Lua Mod loaded!", true)
canvas.log("Hello World Lua Mod started successfully.")

local playerName = "Sky Kid"

function ShowWelcome()
    local btn = gg.alert(
        "Welcome to Canvas Lua Modding!\n\nThis script is running in-process inside Sky.",
        "Open Menu",
        "Dismiss"
    )
    if btn == 1 then
        MainMenu()
    end
end

function MainMenu()
    while true do
        local choice = gg.choice({
            "[1] Say Hello (Toast & Alert)",
            "[2] Set Custom Nickname",
            "[3] System & Game Info",
            "[4] Inspect Memory Regions",
            "[5] Test Memory Scanner",
            "[6] Exit Script"
        }, 1, "Canvas Hello World Menu")

        if choice == nil or choice == 6 then
            gg.toast("Goodbye from Hello World Mod!")
            break
        elseif choice == 1 then
            SayHello()
        elseif choice == 2 then
            SetNamePrompt()
        elseif choice == 3 then
            ShowGameInfo()
        elseif choice == 4 then
            InspectMemoryRegions()
        elseif choice == 5 then
            TestMemorySearch()
        end

        gg.sleep(200)
    end
end

function SayHello()
    gg.toast("Hello, " .. playerName .. "!", false)
    gg.alert("Hello, " .. playerName .. "!\n\nEnjoy modding with Canvas and Lua!", "Awesome")
end

function SetNamePrompt()
    local results = gg.prompt({
        name = "Enter your nickname:"
    }, {
        name = playerName
    }, {
        name = "text"
    })

    if results and results.name and results.name ~= "" then
        playerName = results.name
        gg.toast("Name updated to: " .. playerName)
    end
end

function ShowGameInfo()
    local target = gg.getTargetInfo()
    local version = canvas.getGameVersion()
    local libBase = canvas.getLibBase()

    local infoMsg = string.format(
        "Package: %s\n" ..
        "Game Version: %d\n" ..
        "Process ID: %d\n" ..
        "libBootloader Base: 0x%X",
        target.packageName or "com.tgc.sky.android",
        version,
        target.processId or 0,
        libBase or 0
    )

    gg.alert(infoMsg, "Close")
end

function InspectMemoryRegions()
    local ranges = gg.getRangesList("")
    local count = #ranges
    local sampleInfo = ""

    if count > 0 then
        local first = ranges[1]
        sampleInfo = string.format("\nFirst range: 0x%X - 0x%X (%s)", first.start, first['end'], first.name)
    end

    gg.alert(string.format("Found %d readable memory segments mapped in-process.%s", count, sampleInfo), "OK")
end

function TestMemorySearch()
    gg.toast("Scanning memory for value '100'...")
    gg.clearResults()
    gg.setRanges(gg.REGION_ANONYMOUS | gg.REGION_C_ALLOC | gg.REGION_C_DATA)

    local found = gg.searchNumber("100", gg.TYPE_DWORD, false, gg.SIGN_EQUAL, 0, 0, 50)
    local totalResults = gg.getResultsCount()

    if found and totalResults > 0 then
        local results = gg.getResults(5)
        local summary = string.format("Found %d total results. First %d matches:\n", totalResults, #results)
        for i, res in ipairs(results) do
            summary = summary .. string.format("\n[%d] 0x%X = %s (%s)", i, res.address, res.value, res.name or "")
        end
        gg.alert(summary, "Done")
    else
        gg.alert("No results found for search value '100'.", "OK")
    end
end

-- Start the mod
ShowWelcome()
