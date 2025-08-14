from flask import Flask, request, jsonify

app = Flask(__name__)

TEST_USER = {"email":"test@local","password":"testpass"}
TEST_PRODUCT_KEY = "LOCAL-TEST-PROD-KEY-1234"
REC_TOKEN = "LOCAL-REC-TOKEN-1234"

@app.route("/v1/auth/login", methods=["POST"])
def login():
    j = request.get_json() or {}
    if j.get("email") == TEST_USER["email"] and j.get("password") == TEST_USER["password"]:
        return jsonify(access_token="tok-access-local", refresh_token="tok-refresh-local", rec_token=REC_TOKEN)
    return jsonify(error="invalid"), 401

@app.route("/v1/auth/product_key", methods=["POST"])
def product_key():
    j = request.get_json() or {}
    if j.get("product_key") == TEST_PRODUCT_KEY:
        return jsonify(access_token="tok-access-local", refresh_token="tok-refresh-local", rec_token=REC_TOKEN)
    return jsonify(error="invalid"), 401

@app.route("/v1/entitlement/validate", methods=["POST"])
def validate():
    return jsonify(rec_token=REC_TOKEN)

if __name__ == "__main__":
    app.run(host="127.0.0.1", port=8000)
