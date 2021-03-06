function ServerConnection(url) {
    this.url = url;
    this.closing = false;
    this._onStateChangeCallback = function() {};
    this._onMessageCallback = function(msg) {console.log("message from server: " + msg);};
    this.connect();
}

ServerConnection.prototype.connect = function() {
    this.ws = new WebSocket(this.url);
    console.log("Connecting...");
    this._onStateChangeCallback(this.ws.readyState);

    this.ws.onopen = $.proxy(this._onopen, this);
    this.ws.onclose = $.proxy(this._onclose, this);
    this.ws.onerror = function(e) {console.error("ws error");};
    this.ws.onmessage = $.proxy(this._onmessage, this);
}

ServerConnection.prototype._onopen = function() {
    console.log("Connected!")
    this._onStateChangeCallback(this.ws.readyState);
}

ServerConnection.prototype._onmessage = function(event) {
    this._onMessageCallback(event.data);
}

ServerConnection.prototype.onStateChange = function(callback) {
    this._onStateChangeCallback = callback;
    this._onStateChangeCallback(this.ws.readyState);
}

ServerConnection.prototype.onMessage = function(callback) {
    this._onMessageCallback = callback;
}

ServerConnection.prototype.send = function(data) {
    if (this.ws) {
        try {
            this.ws.send(data);
        } catch (e) {
            console.error("failed to send update to server: " + e.message);
        }
    }
}

ServerConnection.prototype._onclose = function() {
    if (this.closing) return;
    this._onStateChangeCallback(this.ws.readyState);
    console.log("connection closed. Reconnecting...")
    setTimeout($.proxy(function() {
        this.connect();
    }, this), 1000);
}

ServerConnection.prototype.close = function() {
    this.closing = true;
    if (this.ws) {
        this.ws.close();
        this.ws = null;
    }
}
