function ServerConnection(url) {
    this.url = url;
    this.closing = false;
    this._onConnectCallback = function() {};
    this._onMessageCallback = function(event) {console.log("message from server: " + event.data);};
    this.connect();
}

ServerConnection.prototype.connect = function() {
    this.ws = new WebSocket(this.url);
    console.log("Connecting...");

    this.ws.onopen = $.proxy(this._onopen, this);
    this.ws.onclose = $.proxy(this._onclose, this);
    this.ws.onerror = function(e) {console.error("ws error");};
    this.ws.onmessage = $.proxy(this._onmessage, this);
}

ServerConnection.prototype._onopen = function() {
    console.log("Connected!")
    this._onConnectCallback();
}

ServerConnection.prototype.onConnect = function(callback) {
    this._onConnectCallback = callback;
    if (this.ws && this.ws.readyState === this.ws.OPEN) {
        callback();
    }
}

ServerConnection.prototype.onMessage = function(callback) {
    this._onMessageCallback = function(event) {callbacl(event.data);};
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
