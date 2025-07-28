var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
// Init web socket when the page loads
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
    ftime();
    speedmeter(270);
}

//function getReadings(){
//    websocket.send("getReadings");
//}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

// When websocket is established, call the getReadings() function
function onOpen(event) {
    console.log('Connection opened');
    //getReadings();
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

// Function that receives the message from the ESP32 with the readings

bat0_soc=0;
bat1_soc=0;

function onMessage(event) {
    console.log(event.data);
    var myObj = JSON.parse(event.data);
    var keys = Object.keys(myObj);

    let bat0=((myObj || {}).batteries || {})[0];
    if(bat0!=undefined){

        if(bat0.soc_perm != undefined){
            bat0_soc=bat0.soc_perm/10.0;
            document.getElementById("bat0_soc").innerHTML = bat0_soc;
            document.getElementById("soc").innerHTML = (bat0_soc+bat1_soc)/2;
        }

        if(bat0.current_ma != undefined){
            document.getElementById("xxxx").innerHTML = bat0.current_ma/1000.0;
        }

        if(bat0.volt_tot_mv != undefined){
            document.getElementById("bat0_volt").innerHTML = bat0.volt_tot_mv/1000.0;
        }
    }

    let ctrl=(myObj || {}).controller;
    if(ctrl!=undefined){
        //{"controller":{"cur_speed_kph":0,"brake_switch":false,"gear":1,"sliding_backwards":false,"motion":false}}
        if(ctrl.cur_speed_kmh != undefined){
            document.getElementById("speedometer").innerHTML = ctrl.cur_speed_kmh;
            speedmeter((ctrl.cur_speed_kmh/150)*270.0)
        }
        if(ctrl.brake_switch != undefined && ctrl.gear != undefined){
            let lbl="-";
            if(ctrl.brake_switch==false){
                if(ctrl.gear==1){
                    lbl="ECO";
                }else if(ctrl.gear==2){
                    lbl="NORMAL";
                }else if(ctrl.gear==3){
                    lbl="SPORT";
                }
            }
            document.getElementById("mode").innerHTML = lbl;
        }
    }
 /*   for (var i = 0; i < keys.length; i++){
        var key = keys[i];
        document.getElementById(key).innerHTML = myObj[key];
    }*/
}



function ftime() {
    var n = new Date(),
        h = n.getHours(),
        m = n.getMinutes(),
        s = n.getSeconds();
   
    document.getElementById('clock').innerHTML = h + ((s%2)?':':' ') + (m<10?"0":"")+m ;
    setTimeout(ftime, 500);
  }

  

  // All calculations are within 'run' function.
 function speedmeter(angle) {
    const circle = document.querySelector('#speedcircle');
    // 1. Get angle from input field.
    //let angle = parseFloat(input.value) || 0;
  
    // 2. Radius of SVG circle.
    const radius = 200;
    const circumference = 2 * Math.PI * radius;
  
    // 3. First, 1/4 of circumfence of 90 degrees. To start from top of the view,
    //    we must rotate it by 90 degrees. By default circle will start on the right.
    //    Stroke offset effectively rotates the circle.
    // 4. Second, calculate dash array. We need dash array containing only two parts -
    //    visible dash, and invisible dash.
    //    Visible dash should have length of the chosen angle. Full circle is 360 degrees,
    //    and this 360 degrees does also equal the entire circumference. We want just a part of
    //    this entire circle to be visible - (angle / 360 degrees) returns a percentage value
    //    (between 0.0 and 1.0) of how much circumference should be visible.
    //    Hence, we then multiply (angle / 360) times the entire circumference.
    const strokeOffset = (5 / 8) * circumference;
    const strokeDasharray = (angle / 360) * circumference;
  
    // 5. Set circle radius
    circle.setAttribute('r', radius);
    // 6. Create dash array of two elements (combined they must equal the entire circumference).
    //    First has the length of visible portion. Second, the remaining part.
    circle.setAttribute('stroke-dasharray', [
      strokeDasharray,
      circumference - strokeDasharray
    ]);
    // 7. (Optional) Rotate circle to start from the top.
    circle.setAttribute('stroke-dashoffset', strokeOffset);
  }