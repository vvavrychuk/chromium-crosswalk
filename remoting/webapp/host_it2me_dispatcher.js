// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * This class provides an interface between the HostSession and either the
 * NativeMessaging Host
 *
 * TODO(sergeyu): Remove this class.
 */

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/**
 * @constructor
 */
remoting.HostIt2MeDispatcher = function() {
  /**
   * @type {remoting.HostIt2MeNativeMessaging}
   * @private */
  this.nativeMessagingHost_ = null;

  /**
   * @param {remoting.Error} error
   * @private */
  this.onErrorHandler_ = function(error) {}
};

/**
 * @param {function():void} onDispatcherInitialized Callback to be called after
 *     initialization has finished successfully.
 * @param {function(remoting.Error):void} onDispatcherInitializationFailed
 *     Callback to invoke if neither the native messaging host nor the NPAPI
 *     plugin works.
 */
remoting.HostIt2MeDispatcher.prototype.initialize =
    function(onDispatcherInitialized,
             onDispatcherInitializationFailed) {
  /** @type {remoting.HostIt2MeDispatcher} */
  var that = this;

  function onNativeMessagingStarted() {
    onDispatcherInitialized();
  }

  function onNativeMessagingInitFailed() {
    that.nativeMessagingHost_ = null;
    onDispatcherInitializationFailed(remoting.Error.MISSING_PLUGIN);
  }

  this.nativeMessagingHost_ = new remoting.HostIt2MeNativeMessaging();
  this.nativeMessagingHost_.initialize(onNativeMessagingStarted,
                                       onNativeMessagingInitFailed,
                                       this.onNativeMessagingError_.bind(this));
}

/**
 * @param {remoting.Error} error
 */
remoting.HostIt2MeDispatcher.prototype.onNativeMessagingError_ =
    function(error) {
  this.nativeMessagingHost_ = null;
  this.onErrorHandler_(error);
}

/**
 * @param {string} email The user's email address.
 * @param {string} authServiceWithToken Concatenation of the auth service
 *     (e.g. oauth2) and the access token.
 * @param {function(remoting.HostSession.State):void} onStateChanged Callback to
 *     invoke when the host state changes.
 * @param {function(boolean):void} onNatPolicyChanged Callback to invoke when
 *     the nat traversal policy changes.
 * @param {function(string):void} logDebugInfo Callback allowing the plugin
 *     to log messages to the debug log.
 * @param {string} xmppServerAddress XMPP server host name (or IP address) and
 *     port.
 * @param {boolean} xmppServerUseTls Whether to use TLS on connections to the
 *     XMPP server
 * @param {string} directoryBotJid XMPP JID for the remoting directory server
 *     bot.
 * @param {function(remoting.Error):void} onError Callback to invoke in case of
 *     an error.
 */
remoting.HostIt2MeDispatcher.prototype.connect =
    function(email, authServiceWithToken, onStateChanged,
             onNatPolicyChanged, logDebugInfo, xmppServerAddress,
             xmppServerUseTls, directoryBotJid, onError) {
  this.onErrorHandler_ = onError;
  if (this.nativeMessagingHost_) {
    this.nativeMessagingHost_.connect(
        email, authServiceWithToken, onStateChanged, onNatPolicyChanged,
        xmppServerAddress, xmppServerUseTls, directoryBotJid);
  } else {
    console.error(
        'remoting.HostIt2MeDispatcher.connect() without initialization.');
    onError(remoting.Error.UNEXPECTED);
  }
};

/**
 * @return {void}
 */
remoting.HostIt2MeDispatcher.prototype.disconnect = function() {
  this.nativeMessagingHost_.disconnect();
};

/**
 * @return {string} The access code generated by the it2me host.
 */
remoting.HostIt2MeDispatcher.prototype.getAccessCode = function() {
  return this.nativeMessagingHost_.getAccessCode();
};

/**
 * @return {number} The access code lifetime, in seconds.
 */
remoting.HostIt2MeDispatcher.prototype.getAccessCodeLifetime = function() {
  return this.nativeMessagingHost_.getAccessCodeLifetime();
};

/**
 * @return {string} The client's email address.
 */
remoting.HostIt2MeDispatcher.prototype.getClient = function() {
  return this.nativeMessagingHost_.getClient();
};
