const char page_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; text-align: center; background: #f4f4f4; margin: 0; padding-bottom: 50px; }
    
    /* Main Card Style */
    .card { background: white; padding: 20px; display: inline-block; border-radius: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); max-width: 400px; width: 90%; margin-top: 20px; }
    
    /* Input & Button Styles */
    input, button { padding: 10px; margin: 5px 0; width: 90%; box-sizing: border-box; }
    button { cursor: pointer; border: none; border-radius: 4px; color: white; font-weight: bold; font-size: 16px; }
    .btn-save { background-color: #28a745; }
    .btn-del { background-color: #dc3545; display: none; } 
    label { font-weight: bold; display: block; margin-top: 10px; text-align: left; margin-left: 5%; }

    /* WebSocket Status */
    #ws-status { font-size: 12px; color: red; margin-bottom: 10px; font-weight: bold; }

    /* --- AUTH POPUP STYLES --- */
    #authOverlay {
      display: flex; position: fixed; top: 0; left: 0; width: 100%; height: 100%;
      background: rgba(0,0,0,0.9); z-index: 2000;
      justify-content: center; align-items: center;
    }
    .auth-box {
      background: white; width: 80%; max-width: 320px;
      padding: 30px 20px; border-radius: 10px; text-align: center;
      box-shadow: 0 4px 15px rgba(0,0,0,0.3);
    }
    .auth-title { color: #333; margin-top: 0; }
    .btn-login { background-color: #007bff; margin-top: 15px; }
    .btn-signup { background-color: #6c757d; margin-top: 5px; }
    #authError { color: red; font-size: 13px; font-weight: bold; display: none; margin-bottom: 10px; }

    /* --- ALERT POPUP STYLES --- */
    #alertOverlay {
      display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%;
      background: rgba(0,0,0,0.8); z-index: 1000;
    }
    .alert-box {
      background: white; width: 80%; max-width: 300px;
      margin: 100px auto; padding: 20px; border-radius: 10px; border: 4px solid #ff9800;
      text-align: center;
    }
    .alert-title { color: #d35400; font-size: 24px; margin: 0; }
    .btn-snooze { background-color: #f39c12; color: white; margin-bottom: 10px; }
    .btn-take { background-color: #27ae60; color: white; height: 50px; font-size: 18px; }

    /* --- HISTORY LIST STYLES --- */
    #historySection { margin-top: 30px; text-align: left; padding: 0 20px; }
    #historyList { list-style: none; padding: 0; }
    .status-dispensed { color: #d35400; font-weight: bold; }
    .status-taken { color: #27ae60; font-weight: bold; }
  
    .history-item { 
        background: white; border-bottom: 1px solid #ddd; 
        padding: 10px; margin-bottom: 5px; border-radius: 4px;
        font-size: 14px; display: flex; justify-content: space-between; align-items: center;
    }
    .history-details { display: flex; flex-direction: column; text-align: right; }
    .history-time { font-size: 11px; color: #999; }
  </style>
</head>
<body>

<div id="authOverlay">
  <div class="auth-box">
    <h2 class="auth-title">Pill Manager</h2>
    <div id="authError">Invalid credentials</div>
    
    <label>User ID:</label>
    <input type="text" id="authUsername" placeholder="Enter user ID">

    <div id="signupFields" style="display: none;">
        <label>Telegram Chat ID (text /start to @userinfobot):</label>
        <input type="tel" id="authPhone" placeholder="Chat ID">

        <label>Account Type:</label>
        <select id="authRole">
          <option value="Regular">Regular (Manage my own pills)</option>
          <option value="Patient">Patient (View only)</option>
          <option value="Doctor">Doctor (Manage schedules)</option>
        </select>
    </div>
    
    <div style="margin: 15px 0; text-align: left; margin-left: 5%;">
      <input type="checkbox" id="toggleMode" style="width: auto; margin-right: 8px; cursor: pointer;" onchange="toggleAuthView()">
      <label for="toggleMode" style="display: inline; font-weight: normal; cursor: pointer;">I need to create a new account</label>
    </div>
    
    <button id="authSubmitBtn" class="btn-login" onclick="executeAuth()">LOGIN</button>
  </div>
</div>

<div class="card">
  <h2>Pill Manager</h2>
  <div id="ws-status">Disconnected</div>

  <div id="doctorPanel" style="display: none; background: #e3f2fd; padding: 15px; border-radius: 6px; border: 2px solid #2196f3; margin-bottom: 15px;">
    <h3 style="color: #0d47a1; margin-top: 0; margin-bottom: 10px;">Doctor Dashboard</h3>
    <label style="margin-left: 0;">Manage Patient (User ID):</label>
    <input type="text" id="patientId" placeholder="Enter Patient ID" style="width: 100%;">
    <button style="background-color: #0d47a1; margin-top: 10px;" onclick="loadPatient()">Access Patient Data</button>
    <div id="patientStatus" style="color: red; font-size: 13px; font-weight: bold; margin-top: 5px;"></div>
  </div>

  <label>Active User: <span id="displayUser" style="color: #007bff; font-weight: bold;">Not Logged In</span></label>

  <div id="schedulingPanel" style="display: none;">
    <label>Dispenser Slot (0-3):</label>
    <input type="number" id="slot" min="0" max="3" value="0" oninput="requestSlot()">

    <div id="formArea">
      <label>Pill Name:</label>
      <input type="text" id="name" placeholder="Empty Slot">

      <label>Schedule 1:</label>
      <input type="time" id="t1">
      
      <label>Schedule 2:</label>
      <input type="time" id="t2">

      <label>Schedule 3:</label>
      <input type="time" id="t3">

      <button id="saveBtn" class="btn-save" onclick="sendSave()">SAVE</button>
      <button id="delBtn" class="btn-del" onclick="sendDelete()">DELETE PILL</button>
    </div>
  </div> </div>


<div class="card" id="historyCard" style="margin-top: 20px; display: none;">
  <h3>Adherence History</h3>
  <ul id="historyList">
    <li style="color:#aaa; text-align:center;">No pills taken yet</li>
  </ul>
</div>

<div id="alertOverlay">
  <div class="alert-box">
    <h1 class="alert-title">IT'S TIME!</h1>
    <p id="alertMessage">Take your meds</p>
    
    <button class="btn-snooze" onclick="snoozeAlert()">SNOOZE</button>
  </div>
</div>

<script>
  
  // --- WEBSOCKET SETUP ---
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  var historyLog = []; 
  var activePills = []; // Stores names of pills currently ringing

  var loggedInUserId = null;
  var currentUserRole = "Regular";

  window.addEventListener('load', onLoad);

  function onLoad(event) { initWebSocket(); }

  function initWebSocket() {
    console.log('Opening WebSocket...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage;
  }

  function onOpen(event) {
    document.getElementById('ws-status').innerHTML = "Connected";
    document.getElementById('ws-status').style.color = "green";
  }

  function onClose(event) {
    document.getElementById('ws-status').innerHTML = "Disconnected";
    document.getElementById('ws-status').style.color = "red";
    setTimeout(initWebSocket, 2000); // Retry every 2s
  }

  // NEW: send active user to ESP32
  function setUser() {
    if (loggedInUserId === null) {
      console.warn("Attempted to set user, but no user is logged in.");
      return;
    }
    
    // Tell the ESP32 who the active user is
    websocket.send(JSON.stringify({ type: "set_user", user: loggedInUserId }));

    // Refresh current slot view and history for this specific user
    requestSlot();
    websocket.send(JSON.stringify({ type: "get_history" }));
  }


  function toggleAuthView() {
    var isSignup = document.getElementById("toggleMode").checked;
    var fields = document.getElementById("signupFields");
    var btn = document.getElementById("authSubmitBtn");

    if (isSignup) {
      fields.style.display = "block";
      btn.innerText = "CREATE ACCOUNT";
      btn.className = "btn-signup"; 
    } else {
      fields.style.display = "none";
      btn.innerText = "LOGIN";
      btn.className = "btn-login"; 
    }
  }

  // --- NEW: Determine which action to send ---
  function executeAuth() {
    var isSignup = document.getElementById("toggleMode").checked;
    sendAuth(isSignup ? 'signup' : 'login');
  }

  function sendAuth(action) {
    var u = document.getElementById("authUsername").value.trim();
    var r = document.getElementById("authRole").value;
    var p = document.getElementById("authPhone").value.trim()
    
    if (u === "") {
        showAuthError("Please enter username.");
        return;
    }
    websocket.send(JSON.stringify({ type: action, username: u, role: r, phone: p }));
  }

  function showAuthError(msg) {
    var errBox = document.getElementById("authError");
    errBox.innerHTML = msg;
    errBox.style.display = "block";
  }

  function loadPatient() {
    var p = document.getElementById("patientId").value.trim();
    if(p === "") {
        document.getElementById("patientStatus").innerText = "Enter a patient ID";
        document.getElementById("patientStatus").style.color = "red";
        return;
    }
    document.getElementById("patientStatus").innerText = "Searching...";
    document.getElementById("patientStatus").style.color = "#007bff";
    websocket.send(JSON.stringify({ type: "load_patient", patient: p }));
  }

  // --- HANDLING MESSAGES FROM ESP32 ---
  function onMessage(event) {
    console.log("RX: " + event.data);
    var data = JSON.parse(event.data);
    // 0. AUTHENTICATION RESPONSES
    if (data.type === "auth_success") {
        document.getElementById("authOverlay").style.display = "none";
        currentUserRole = data.role;
        document.getElementById("displayUser").innerText = data.username + " (" + currentUserRole + ")";
        
        loggedInUserId = data.user_id; 

        if (currentUserRole === "Doctor") {
            // Doctors ONLY see the search panel on login
            document.getElementById("doctorPanel").style.display = "block";
            document.getElementById("schedulingPanel").style.display = "none";
            document.getElementById("historyCard").style.display = "none";
        } else {
            // Regular users & Patients see everything immediately
            document.getElementById("doctorPanel").style.display = "none";
            document.getElementById("schedulingPanel").style.display = "block";
            document.getElementById("historyCard").style.display = "inline-block";
            
            setUser(); 
        }
    }
    else if (data.type === "auth_fail") {
        showAuthError(data.msg || "Authentication failed.");
    }
    else if (data.type === "patient_success") {
        document.getElementById("patientStatus").innerText = "Currently managing: " + data.patient;
        document.getElementById("patientStatus").style.color = "green";
        
        // We found a patient! Unhide the dashboards so the Doctor can see the incoming data.
        document.getElementById("schedulingPanel").style.display = "block";
        document.getElementById("historyCard").style.display = "inline-block";
    }
    else if (data.type === "patient_fail") {
        document.getElementById("patientStatus").innerText = "Patient not found or invalid role.";
        document.getElementById("patientStatus").style.color = "red";
    }

    // 1. DATA LOAD (Populate inputs)
    if (data.type === "slot_data") {
        document.getElementById("name").value = data.name;
        document.getElementById("t1").value = data.t1;
        document.getElementById("t2").value = data.t2;
        document.getElementById("t3").value = data.t3;

        // UI Locking Logic
        var isLocked = (data.name !== "");
        var isPatient = (currentUserRole === "Patient");
        
        // // Toggle Buttons: Show Delete if locked, Show Save if unlocked
        // document.getElementById("delBtn").style.display = isLocked ? "inline-block" : "none";
        // document.getElementById("saveBtn").style.display = isLocked ? "none" : "inline-block";
        
        // // Lock the inputs
        // document.getElementById("name").disabled = isLocked;
        // document.getElementById("name").style.backgroundColor = isLocked ? "#e9ecef" : "white";
        // document.getElementById("t1").disabled = isLocked;
        // document.getElementById("t2").disabled = isLocked;
        // document.getElementById("t3").disabled = isLocked;

        // If Patient: ALWAYS hide buttons. If Doctor/Regular: toggle based on lock.
        document.getElementById("delBtn").style.display = (isLocked && !isPatient) ? "inline-block" : "none";
        document.getElementById("saveBtn").style.display = (!isLocked && !isPatient) ? "inline-block" : "none";
        
        // If Patient: ALWAYS disable inputs. If Doctor/Regular: toggle based on lock.
        var disableInputs = (isLocked || isPatient);
        document.getElementById("name").disabled = disableInputs;
        document.getElementById("name").style.backgroundColor = disableInputs ? "#e9ecef" : "white";
        document.getElementById("t1").disabled = disableInputs;
        document.getElementById("t2").disabled = disableInputs;
        document.getElementById("t3").disabled = disableInputs;
    }

    // 2. ALERT TRIGGER (ESP32 says "Dispense Now!")
    else if (data.type === "alert") {
      // We now expect the ESP32 to send slot and time along with the name
      handleIncomingAlert(data.msg, data.slot, data.time);
    }
    else if (data.type === "close_alert") {
        document.getElementById("alertOverlay").style.display = "none";
        activePills = []; // Clear active pills so the snooze timer doesn't trigger
    }
    else if (data.type === "refresh_slots") {
        console.log("STM32 finished loading SD data. Refreshing UI...");
        requestSlot(); 
    }
    else if (data.type === "refresh_history") {
        // ESP32 says: "History changed, please ask for the new list"
        websocket.send(JSON.stringify({type: "get_history"}));
    }

    // --- NEW: RECEIVE FULL HISTORY LIST ---
    else if (data.type === "history_full") {
        renderHistory(data.data);
    }
    else if (data.type === "hardware_taken") {
        // Send the ENTIRE array in one single message
      websocket.send(JSON.stringify({ 
          type: "log_taken", 
          pills: activePills 
      }));
      
      document.getElementById("alertOverlay").style.display = "none";
      activePills = [];
    }
  }

  // --- SENDING COMMANDS TO ESP32 ---
  
  function requestSlot() {
    var slot = document.getElementById("slot").value;
    // Debounce or basic check could go here
    websocket.send(JSON.stringify({ type: "get", slot: parseInt(slot) }));
  }

  function sendSave() {
    var slot = parseInt(document.getElementById("slot").value);
    var name = document.getElementById("name").value;
    if(name === "") { alert("Enter Name"); return; }

    var msg = {
        type: "save",
        slot: slot,
        name: name,
        t1: document.getElementById("t1").value,
        t2: document.getElementById("t2").value,
        t3: document.getElementById("t3").value
    };
    websocket.send(JSON.stringify(msg));
  }

  function sendDelete() {
    if(!confirm("Delete this pill?")) return;
    var msg = {
        type: "delete",
        slot: parseInt(document.getElementById("slot").value)
    };
    websocket.send(JSON.stringify(msg));
  }

  // --- ALERT & HISTORY LOGIC ---

  function handleIncomingAlert(pillName, pillSlot, pillTime) {
    // Check if it's already in the list
    var exists = activePills.find(p => p.name === pillName);
    if(!exists) {
        // Store as an object so we remember the slot and time
        activePills.push({ name: pillName, slot: pillSlot, time: pillTime });
    }
    updateAlertBox();
  }

  function updateAlertBox() {
    if (activePills.length === 0) return;
    
    // Extract just the names to display on the screen
    var names = activePills.map(p => p.name).join(" & ");
    var msg = "Time to take:<br><b>" + names + "</b>";
    
    document.getElementById("alertMessage").innerHTML = msg;
    document.getElementById("alertOverlay").style.display = "block";
  }

  function snoozeAlert() {
    document.getElementById("alertOverlay").style.display = "none";
    
    // Send the ENTIRE array in one single WebSocket message
    websocket.send(JSON.stringify({ 
       type: "snooze_alert", 
       pills: activePills 
    }));
  }

  function renderHistory(historyArray) {
    var listHTML = "";
    // Loop through data sent by ESP32
    for (var i = 0; i < historyArray.length; i++) {
      var item = historyArray[i];
      var cssClass = (item.type === "Dispensed") ? "status-dispensed" : "status-taken";

      listHTML += `<li class="history-item">
                     <span><b>${item.name}</b></span>
                     <div class="history-details">
                       <span class="${cssClass}">${item.type}</span>
                       <span class="history-time">${item.time}</span>
                     </div>
                   </li>`;
    }
    
    if(historyArray.length === 0) {
        listHTML = `<li style="color:#aaa; text-align:center;">No pills taken yet</li>`;
    }
    
    document.getElementById("historyList").innerHTML = listHTML;
  }

</script>
</body>
</html>
)=====";
