var density = 'ultra';

var legend_canvas = document.getElementById('legend');
var legend_ctx = legend_canvas.getContext('2d');
legend_ctx.lineWidth = 1;
legend_ctx.strokeStyle = "#ccccee";
if (legend_ctx.setLineDash) {
  legend_ctx.setLineDash([2,2]);
}

var memory_canvas = document.getElementById('memory');
var memory_ctx = memory_canvas.getContext('2d');
memory_ctx.textAlign = "end";

var animation_canvas = document.getElementById('animation');
var animation_ctx = animation_canvas.getContext('2d');
animation_ctx.lineJoin = 'round';

var drawing_canvas = document.getElementById('drawing');
var drawing_ctx = drawing_canvas.getContext('2d');
drawing_ctx.lineJoin = 'round';

var word_height;
var word_width;
var label_offset_x;
var label_offset_y;

if (density == 'low') {
  word_height = 30;
  word_width = 80;
  label_offset_x = word_width - 8;
  label_offset_y = word_height - 8;
  memory_ctx.font = "24px Helvetica,sans-serif;"
  animation_ctx.lineWidth = 3;
}
else if (density == 'high') {
  word_height = 16;
  word_width = 40;
  label_offset_x = word_width - 2;
  label_offset_y = word_height - 3;
  memory_ctx.font = "14px Helvetica,sans-serif;"
  animation_ctx.lineWidth = 2;
}
else if (density == 'ultra') {
  word_height = 12;
  word_width = 30;
  label_offset_x = word_width - 2;
  label_offset_y = word_height - 2;
  memory_ctx.font = "10px Helvetica,sans-serif"
  animation_ctx.lineWidth = 2;
}
else { // default medium density
  word_height = 20;
  word_width = 50;
  label_offset_x = word_width - 4;
  label_offset_y = word_height - 4;
  memory_ctx.font = "16px Helvetica,sans-serif;"
  animation_ctx.lineWidth = 3;
}


var word_half_height = Math.floor(word_height / 2);
var word_half_width = Math.floor(word_width / 2);
var memory_width = Math.floor(memory_canvas.width / word_width)

function addressToPoint(addr) {
  var y = Math.floor(addr / memory_width);
  var x = addr - y * memory_width;
  return [x, y];
}

function drawGuides() {
  var end_x = legend_canvas.width;
  var end_y = legend_canvas.height;
  for (var x = 0.5; x <= end_x; x += word_width) {
    legend_ctx.beginPath();
    legend_ctx.moveTo(x, 0.5);
    legend_ctx.lineTo(x, end_y);
    legend_ctx.closePath();
    legend_ctx.stroke();
  }
  for (var y = 0.5; y <= end_y; y += word_height) {
    legend_ctx.beginPath();
    legend_ctx.moveTo(0.5, y);
    legend_ctx.lineTo(end_x, y);
    legend_ctx.closePath();
    legend_ctx.stroke();
  }
}

var mem_content = [
 ":nil"
];

function drawWord(addr) {
  var p = addressToPoint(addr);
  var x = p[0] * word_width + 1;
  var y = p[1] * word_height + 1;

  memory_ctx.fillStyle = "#222222";
  memory_ctx.fillRect(x, y, word_width - 1, word_height - 1);

  var cell = mem_content[addr].toString();
  if (cell) {
    if (cell.charAt(0) == "'") {
      memory_ctx.fillStyle = "#00ff00";
      cell = cell.substring(1);
    }
    else if (cell.charAt(0) == "=") {
      memory_ctx.fillStyle = "#00ff00";
      cell = cell.substring(1);
    }
    else if (cell.charAt(0) == ":") {
      memory_ctx.fillStyle = "#ffffff";
      cell = cell.substring(1);
    }
    else {
      memory_ctx.fillStyle = "#ffff00";
      var c = addressToPoint(mem_content[addr]);
      cell = c[1].toString() + "." + c[0];
    }

    memory_ctx.fillText(cell, x + label_offset_x, y + label_offset_y);
  }
}

function clearWord(addr) {
  var p = addressToPoint(addr);
  var x = p[0] * word_width + 1;
  var y = p[1] * word_height + 1;

  memory_ctx.fillStyle = "#ffffff";
  memory_ctx.fillRect(x, y, word_width - 1, word_height - 1);
}

function copyWords(to_addr, from_addr, count, subframe) {
  if (subframe >= 100) {
    for (var i = 0; i < count; ++i) {
      drawWord(to_addr + i);
    }
  }
  else {
    animation_ctx.fillStyle = "#000000";
    animation_ctx.strokeStyle = "#00ff00";

    var percent = subframe / 100;
    for (var i = 0; i < count; ++i) {
      var from_p = addressToPoint(from_addr + i);
      var from_x = from_p[0] * word_width;
      var from_y = from_p[1] * word_height;
      var from_mid_x = from_x + word_half_width;
      var from_mid_y = from_y + word_half_height;

      var to_p = addressToPoint(to_addr + i);
      var to_x = to_p[0] * word_width;
      var to_y = to_p[1] * word_height;

      var delta_x = Math.round((to_x - from_x) * percent);
      var delta_y = Math.round((to_y - from_y) * percent);

      animation_ctx.fillRect(from_x + delta_x, from_y + delta_y,
                             word_width - 1, word_height - 1);

      if (i == 0) {
        animation_ctx.beginPath();
        animation_ctx.moveTo(from_mid_x, from_mid_y);
        animation_ctx.lineTo(from_mid_x + delta_x, from_mid_y + delta_y);
        animation_ctx.stroke();
      }
    }
  }
}

function blinkWords(list, subframe, color) {
  if (subframe < 100 && subframe % 20 != 0) {
    animation_ctx.strokeStyle = color;

    for (var i = 1; i < list.length; ++i) {
      var p = addressToPoint(list[i]);
      var x = p[0] * word_width;
      var y = p[1] * word_height;

      animation_ctx.strokeRect(x, y, word_width + 1, word_height + 1);
    }
  }
}

drawGuides();
drawWord(0);

function draw_connection(event) {
  var totalOffsetX = 0;
  var totalOffsetY = 0;
  var currentElement = this;

  do {
    totalOffsetX += currentElement.offsetLeft;
    totalOffsetY += currentElement.offsetTop;
  }
  while (currentElement = currentElement.offsetParent);

  //var x = Math.floor((event.pageX - totalOffsetX) / word_width);
  //var y = Math.floor((event.pageY - totalOffsetY) / word_height);
  var x = Math.floor(event.layerX / word_width);
  var y = Math.floor(event.layerY / word_height);
  var addr = y * memory_width + x;

  x = x * word_width;
  y = y * word_height;

  drawing_ctx.clearRect(0, 0, drawing_canvas.width, drawing_canvas.height);

  if (mem_content[addr]) {
    drawing_ctx.lineWidth = 3;
    drawing_ctx.strokeStyle = "#ffffff";
    drawing_ctx.strokeRect(x, y, word_width + 1, word_height + 1);

    var cell = mem_content[addr].toString();
    var to_x;
    var to_y;

    if (cell.charAt(0) >= '0' && cell.charAt(0) <= '9') {
      var p = addressToPoint(mem_content[addr]);
      to_x = p[0] * word_width;
      to_y = p[1] * word_height;
    }

    if (to_x || to_y) {
      drawing_ctx.lineWidth = 1;
      drawing_ctx.beginPath();
      drawing_ctx.moveTo(x + word_half_width, y + word_half_height);
      drawing_ctx.lineTo(to_x + word_half_width, to_y + word_half_height);
      drawing_ctx.stroke();
    }
  }
}

drawing_canvas.addEventListener("mousedown", draw_connection, false);

var requestAnimationFrame = (function() {
  return window.requestAnimationFrame       ||
         window.webkitRequestAnimationFrame ||
         window.mozRequestAnimationFrame    ||
         window.oRequestAnimationFrame      ||
         window.msRequestAnimationFrame     ||
         function(callback) {
           window.setTimeout(callback, 1000 / 60);
         };
})();

var animation_running = false;
var frame = 0;
var subframe = 0;
var frame_count = frame_content.length;

function toggle_animation() {
  var button = document.getElementById('run_button');
  animation_running = !animation_running;
  if (animation_running) {
    button.innerHTML = "Pause";
    requestAnimationFrame(animate);
  }
  else {
    button.innerHTML = "Continue";
  }
  var status = document.getElementById('run_status');
  status.innerHTML = "";
}

function pause_animation(message) {
  var button = document.getElementById('run_button');
  animation_running = false;
  button.innerHTML = "Continue";
  var status = document.getElementById('run_status');
  status.innerHTML = message;
}

function stop_animation() {
  var button = document.getElementById('run_button');
  frame = 0;
  animation_running = false;
  button.innerHTML = "Start";
  var status = document.getElementById('run_status');
  status.innerHTML = "";
  var bp_msg = document.getElementById('bp_msg');
  bp_msg.innerHTML = "";
}

function animate(timestamp) {
  if (animation_running && frame < frame_count) {
    requestAnimationFrame(animate);
    animation_ctx.clearRect(0, 0, animation_canvas.width, animation_canvas.height);

    if (subframe == 0) {
      if (frame_content[frame][0] == 'box') {
        mem_content[frame_content[frame][1]] = frame_content[frame][2];
        drawWord(frame_content[frame][1]);
      }
      else if (frame_content[frame][0] == 'set') {
        mem_content[frame_content[frame][1]] = frame_content[frame][2];
        drawWord(frame_content[frame][1]);
      }
      else if (frame_content[frame][0] == 'alloc') {
        for (var i = 0; i < frame_content[frame][2]; ++i) {
          mem_content[frame_content[frame][1] + i] = "";
          drawWord(frame_content[frame][1] + i);
        }
      }
      else if (frame_content[frame][0] == 'free') {
        for (var i = 0; i < frame_content[frame][2]; ++i) {
          mem_content[frame_content[frame][1] + i] = "";
          clearWord(frame_content[frame][1] + i);
        }
      }
      else if (frame_content[frame][0] == 'ref_count') {
      }
      else if (frame_content[frame][0] == 'bp') {
        var bp_msg = document.getElementById('bp_msg');
        bp_msg.innerHTML = frame_content[frame][1];
      }
      else if (frame_content[frame][0] == 'roots') {
        blinkWords(frame_content[frame], 1, "#ff0000");
        pause_animation("root set");
      }
      else if (frame_content[frame][0] == 'live') {
        blinkWords(frame_content[frame], 1, "#ff0000");
        pause_animation("live set");
      }
      else if (frame_content[frame][0] == 'stop') {
        stop_animation();
      }
      else {
        // begin a subframe animation
        subframe += 10;
        if (frame_content[frame][0] == 'copy') {
          for (var i = 0; i < frame_content[frame][3]; ++i) {
            mem_content[frame_content[frame][1] + i] = mem_content[frame_content[frame][2] + i];
          }
          copyWords(frame_content[frame][1], frame_content[frame][2], frame_content[frame][3], subframe);
        }
      }
      if (subframe == 0) {
        ++frame;
      }
    }
    else {
      subframe += 10;
      if (frame_content[frame][0] == 'copy') {
        copyWords(frame_content[frame][1], frame_content[frame][2], frame_content[frame][3], subframe);
      }
      else if (frame_content[frame][0] == 'roots') {
        blinkWords(frame_content[frame], subframe, "#ff0000");
      }
      else if (frame_content[frame][0] == 'live') {
        blinkWords(frame_content[frame], subframe, "#ff0000");
      }
      if (subframe >= 100) {
        subframe = 0;
        ++frame;
      }
    }
  }
}
