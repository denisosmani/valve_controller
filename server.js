var connection = new WebSocket('ws://' + location.hostname + ':81/');

connection.onopen = function() {
  document.getElementById("wsStatusCircle").style.backgroundColor = "green";
};

connection.onmessage = function(event) {
  var message = JSON.parse(event.data);
  
  if (message.type === "init") {
    // Initialize all valve states
    message.valves.forEach((status, index) => {
      var valveElement = document.getElementById("valve" + index + "Status");
      valveElement.className = "valveCircle " + (status ? "valveON" : "valveOFF");
    });
  } else if (message.type === "update") {
    // Update a single valve state
    var valveElement = document.getElementById("valve" + message.valve + "Status");
    valveElement.className = "valveCircle " + (message.status ? "valveON" : "valveOFF");
  }
};

connection.onerror = function(error) {
  console.error("WebSocket Error:", error);
};

function toggleValve(valveIndex) {
  var command = JSON.stringify({
    type: "toggle",
    valve: valveIndex
  });
  connection.send(command);
}

function startAutoControl() {
  var timeDuration = document.getElementById('duration').value;
  var command = JSON.stringify({
    type: "startAuto",
    duration: timeDuration
  });
    connection.send(command);
}

function stopAutoControl() {
  var command = JSON.stringify({
    type: "stopAuto"
  });
  connection.send(command);
}