# Desktop-auth server with GUI + embedded Flask endpoints
# Replaces SQLAlchemy usage with a simple JSON-backed store so this can run as a desktop app

from flask import Flask, request, jsonify
from werkzeug.security import generate_password_hash, check_password_hash
import jwt
import datetime
import os
import json
import threading
import tkinter as tk
from tkinter import simpledialog, messagebox, scrolledtext, filedialog

DATA_FILE = os.path.join(os.path.dirname(__file__), 'server_data.json')
SECRET_KEY = os.environ.get('SECRET_KEY', 'your_secret_key')

app = Flask(__name__)
app.config['SECRET_KEY'] = SECRET_KEY

# In-memory store loaded from DATA_FILE
store = {
    'users': {},        # email -> {password_hash, id}
    'licenses': {},     # token -> {email}
    'next_user_id': 1
}

# Utility functions for persistence
def load_store():
    global store
    if os.path.exists(DATA_FILE):
        try:
            with open(DATA_FILE, 'r', encoding='utf-8') as f:
                store = json.load(f)
        except Exception:
            store = {'users': {}, 'licenses': {}, 'next_user_id': 1}

def save_store():
    try:
        with open(DATA_FILE, 'w', encoding='utf-8') as f:
            json.dump(store, f, indent=2)
    except Exception as e:
        print('Failed to save store:', e)

# Flask endpoints (same semantics as earlier server)
@app.route('/v1/auth/login', methods=['POST'])
def login():
    j = request.get_json() or {}
    email = j.get('email') or j.get('username') or j.get('user')
    password = j.get('password')
    if not email or not password:
        return jsonify({'error': 'missing credentials'}), 400

    user = store['users'].get(email)
    if user and check_password_hash(user['password_hash'], password):
        token = jwt.encode({'user_id': user['id'], 'exp': datetime.datetime.utcnow() + datetime.timedelta(hours=1)}, app.config['SECRET_KEY'])
        # attempt to find a license token
        rec = None
        for tok, info in store['licenses'].items():
            if info.get('email') == email:
                rec = tok
                break
        return jsonify(access_token=token, refresh_token='refresh-'+str(user['id']), rec_token=(rec or ''))
    return jsonify({'error': 'invalid'}), 401

@app.route('/v1/auth/product_key', methods=['POST'])
def product_key():
    j = request.get_json() or {}
    key = j.get('product_key') or j.get('key')
    if not key:
        return jsonify({'error': 'missing product_key'}), 400
    lic = store['licenses'].get(key)
    if lic:
        # issue tokens for owner
        email = lic.get('email')
        user = store['users'].get(email)
        uid = user['id'] if user else 0
        token = jwt.encode({'user_id': uid, 'exp': datetime.datetime.utcnow() + datetime.timedelta(hours=1)}, app.config['SECRET_KEY'])
        return jsonify(access_token=token, refresh_token='refresh-'+str(uid), rec_token=key)
    return jsonify({'error': 'invalid'}), 401

@app.route('/v1/entitlement/validate', methods=['POST'])
def validate_entitlement():
    auth = request.headers.get('Authorization','')
    token = ''
    if auth and auth.startswith('Bearer '):
        token = auth.split()[1]
    # For local desktop validation, we accept any token if a rec_token exists for the user or return an empty rec_token
    # If Authorization header is absent, allow returning a rec_token for local testing
    # Find rec_token by inspecting request JSON (optional)
    j = request.get_json() or {}
    rec = j.get('rec_token')
    if rec and rec in store['licenses']:
        return jsonify(rec_token=rec)

    # attempt to map bearer token to a rec_token
    if token:
        try:
            data = jwt.decode(token, app.config['SECRET_KEY'], algorithms=["HS256"])
            uid = data.get('user_id')
            # find user email
            for email, u in store['users'].items():
                if u.get('id') == uid:
                    # find license for this email
                    for tok, info in store['licenses'].items():
                        if info.get('email') == email:
                            return jsonify(rec_token=tok)
        except Exception:
            pass

    # fallback: return empty rec_token
    return jsonify(rec_token='')

@app.route('/v1/auth/refresh', methods=['POST'])
def refresh_token():
    auth = request.headers.get('Authorization','')
    if not auth.startswith('Bearer '):
        return jsonify({'error':'missing'}), 400
    token = auth.split()[1]
    try:
        data = jwt.decode(token, app.config['SECRET_KEY'], algorithms=["HS256"], options={"verify_exp": False})
        new_token = jwt.encode({'user_id': data.get('user_id'), 'exp': datetime.datetime.utcnow() + datetime.timedelta(hours=1)}, app.config['SECRET_KEY'])
        return jsonify(access_token=new_token)
    except Exception:
        return jsonify({'error':'invalid'}), 401

# Utilities for GUI and CLI management
def create_user(email, password):
    if email in store['users']:
        return False
    uid = store.get('next_user_id',1)
    store['users'][email] = {'password_hash': generate_password_hash(password), 'id': uid}
    store['next_user_id'] = uid + 1
    save_store()
    return True

def create_license_for_email(token, email):
    store['licenses'][token] = {'email': email}
    save_store()

# Embedded server runner
_server_thread = None
_server_should_run = False

def run_flask_server(host='127.0.0.1', port=8000):
    # use a non-reloading single-threaded server for embedded use
    app.run(host=host, port=port, debug=False, use_reloader=False)

def start_server(host='127.0.0.1', port=8000):
    global _server_thread, _server_should_run
    if _server_thread and _server_thread.is_alive():
        return False
    _server_should_run = True
    _server_thread = threading.Thread(target=run_flask_server, args=(host,port), daemon=True)
    _server_thread.start()
    return True

# Simple Tkinter GUI
class ServerGUI:
    def __init__(self, root):
        self.root = root
        root.title('Local Auth Desktop')

        frm = tk.Frame(root)
        frm.pack(padx=8, pady=8)

        tk.Label(frm, text='Server Host:').grid(row=0, column=0, sticky='e')
        self.hostEntry = tk.Entry(frm)
        self.hostEntry.insert(0,'127.0.0.1')
        self.hostEntry.grid(row=0,column=1)

        tk.Label(frm, text='Port:').grid(row=0, column=2, sticky='e')
        self.portEntry = tk.Entry(frm, width=6)
        self.portEntry.insert(0,'8000')
        self.portEntry.grid(row=0,column=3)

        self.startBtn = tk.Button(frm, text='Start Server', command=self.start_server)
        self.startBtn.grid(row=0, column=4, padx=6)

        tk.Label(frm, text='New user email:').grid(row=1, column=0, sticky='e')
        self.emailEntry = tk.Entry(frm)
        self.emailEntry.grid(row=1, column=1, columnspan=2, sticky='we')

        tk.Label(frm, text='Password:').grid(row=1, column=3, sticky='e')
        self.pwEntry = tk.Entry(frm, show='*')
        self.pwEntry.grid(row=1, column=4)

        self.addUserBtn = tk.Button(frm, text='Add User', command=self.add_user)
        self.addUserBtn.grid(row=1, column=5, padx=6)

        tk.Label(frm, text='Product key:').grid(row=2, column=0, sticky='e')
        self.keyEntry = tk.Entry(frm)
        self.keyEntry.grid(row=2, column=1, columnspan=2, sticky='we')
        self.assignKeyEmail = tk.Entry(frm)
        self.assignKeyEmail.grid(row=2, column=3, columnspan=2, sticky='we')
        self.addKeyBtn = tk.Button(frm, text='Add Key', command=self.add_key)
        self.addKeyBtn.grid(row=2, column=5, padx=6)

        tk.Label(frm, text='Logs:').grid(row=3, column=0, sticky='nw')
        self.logBox = scrolledtext.ScrolledText(root, width=80, height=12)
        self.logBox.pack(padx=8, pady=6)
        self.log('Loaded store with %d users and %d licenses' % (len(store['users']), len(store['licenses'])))

        btnFrm = tk.Frame(root)
        btnFrm.pack(pady=6)
        tk.Button(btnFrm, text='Save Store As...', command=self.save_as).pack(side='left', padx=6)
        tk.Button(btnFrm, text='Load Store...', command=self.load_store).pack(side='left', padx=6)
        tk.Button(btnFrm, text='Quit', command=self.quit).pack(side='right', padx=6)

    def log(self, s):
        self.logBox.insert('end', s + '\n')
        self.logBox.see('end')
        print(s)

    def start_server(self):
        host = self.hostEntry.get() or '127.0.0.1'
        try:
            port = int(self.portEntry.get())
        except Exception:
            messagebox.showerror('Invalid port','Port must be a number')
            return
        ok = start_server(host, port)
        if ok:
            self.log('Server started on %s:%d' % (host, port))
            self.startBtn.config(state='disabled')
        else:
            self.log('Server already running')

    def add_user(self):
        email = self.emailEntry.get().strip()
        pw = self.pwEntry.get()
        if not email or not pw:
            messagebox.showerror('Missing','Provide email and password')
            return
        if create_user(email,pw):
            self.log('Created user %s' % email)
        else:
            self.log('User already exists')

    def add_key(self):
        key = self.keyEntry.get().strip()
        email = self.assignKeyEmail.get().strip()
        if not key or not email:
            messagebox.showerror('Missing','Provide key and email to assign')
            return
        create_license_for_email(key,email)
        self.log('Created key %s -> %s' % (key,email))

    def save_as(self):
        path = filedialog.asksaveasfilename(defaultextension='.json')
        if not path: return
        try:
            with open(path,'w',encoding='utf-8') as f:
                json.dump(store,f,indent=2)
            self.log('Saved store to %s' % path)
        except Exception as e:
            messagebox.showerror('Error','Failed to save: %s' % e)

    def load_store(self):
        path = filedialog.askopenfilename(filetypes=[('JSON files','*.json'),('All files','*.*')])
        if not path: return
        try:
            with open(path,'r',encoding='utf-8') as f:
                data = json.load(f)
                store.clear(); store.update(data)
            save_store()
            self.log('Loaded store from %s' % path)
        except Exception as e:
            messagebox.showerror('Error','Failed to load: %s' % e)

    def quit(self):
        self.log('Exiting...')
        self.root.quit()


if __name__ == '__main__':
    load_store()
    # Launch GUI
    root = tk.Tk()
    gui = ServerGUI(root)
    root.mainloop()