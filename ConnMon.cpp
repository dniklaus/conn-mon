/*
 * ConnMon.cpp
 *
 *  Created on: 08.02.2018
 *      Author: dniklaus
 */

#include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#include <Timer.h>
#include <DbgTracePort.h>
#include <DbgTraceLevel.h>

#include <ConnMon.h>

class StatusPollTimerAdapter : public TimerAdapter
{
private:
  ConnMon* m_monitor;

public:
  StatusPollTimerAdapter(ConnMon* monitor)
  : m_monitor(monitor)
  { }

  void timeExpired()
  {
    if (0 != m_monitor)
    {
      m_monitor->evaluateState();
    }
  }
};

class StableCheckPollTimerAdapter : public TimerAdapter
{
private:
  ConnMon* m_monitor;

public:
  StableCheckPollTimerAdapter(ConnMon* monitor)
  : m_monitor(monitor)
  { }

  void timeExpired()
  {
    if ((0 != m_monitor) && (0 != m_monitor->state()))
    {
      m_monitor->state()->timeExpired(m_monitor);
    }
  }
};

//-----------------------------------------------------------------------------

ConnMonAdapter::ConnMonAdapter()
: m_trPort(new DbgTrace_Port("conmon", DbgTrace_Level::info))
{ }

ConnMonAdapter::~ConnMonAdapter()
{
  delete m_trPort;
  m_trPort = 0;
}

DbgTrace_Port* ConnMonAdapter::trPort()
{
  return m_trPort;
}

bool ConnMonAdapter::lanConnectedRaw()
{
  bool isLanConnected = false;
#ifdef ESP8266
  isLanConnected = WiFi.isConnected();
#endif
  TR_PRINTF(trPort(), DbgTrace_Level::debug, "WiFi device is %sconnected", (isLanConnected ? "" : "dis"));
  return isLanConnected;
}

bool ConnMonAdapter::appProtocolConnectedRaw()
{
  return false;
}

//-----------------------------------------------------------------------------

const unsigned long cStatusPollIntervalMillis  = 1000;
const unsigned long cStableCheckIntervalMillis = 2000;

ConnMon::ConnMon(ConnMonAdapter* adapter)
: m_statusPollTimer(new Timer(new StatusPollTimerAdapter(this), Timer::IS_RECURRING, cStatusPollIntervalMillis))
, m_stableConnCheckTimer(new Timer(new StableCheckPollTimerAdapter(this), Timer::IS_NON_RECURRING, cStableCheckIntervalMillis))
, m_adapter(adapter)
, m_state(ConnMonState_Unconnected::Instance())
, m_prevState(ConnMonState_Unconnected::Instance())
{
  if (0 == m_adapter)
  {
    new ConnMonAdapter();
  }
}

ConnMon::~ConnMon()
{
  delete m_adapter;
  m_adapter = 0;

  delete m_statusPollTimer->adapter();
  m_statusPollTimer->attachAdapter(0);

  delete m_statusPollTimer;
  m_statusPollTimer = 0;

  delete m_stableConnCheckTimer->adapter();
  m_stableConnCheckTimer->attachAdapter(0);

  delete m_stableConnCheckTimer;
  m_stableConnCheckTimer = 0;
}

ConnMonAdapter* ConnMon::adapter()
{
  return m_adapter;
}

bool ConnMon::isLanDeviceConnected()
{
  bool isConn = false;
  if (0 != m_adapter)
  {
    isConn = m_adapter->lanConnectedRaw();
  }
  return isConn;
}

bool ConnMon::isAppProtocolLibConnected()
{
  bool isConn = false;
  if (0 != m_adapter)
  {
    isConn = m_adapter->appProtocolConnectedRaw();
  }
  return isConn;
}

bool ConnMon::isLanConnected()
{
  return (ConnMonState_StableLanConnection::Instance() == state());
}

bool ConnMon::isAppProtocolConnected()
{
  return (ConnMonState_AppProtocolConnected::Instance() == state());
}

void ConnMon::evaluateState()
{
  if (0 != m_state)
  {
    m_state->evaluateState(this);
  }
}

void ConnMon::setAppProtocolState(bool appProtocolIsConnected)
{
  if (0 != m_state)
  {
    m_state->evaluateState(this, appProtocolIsConnected);
  }
}

void ConnMon::startStableLanConnCheckTimer()
{
  m_stableConnCheckTimer->startTimer(cStableCheckIntervalMillis);
}

void ConnMon::changeState(ConnMonState* newState)
{
  m_prevState = m_state;
  m_state = newState;
  if (0 != newState)
  {
    newState->entry(this);
  }
}

ConnMonState* ConnMon::state()
{
  return m_state;
}

ConnMonState* ConnMon::prevState()
{
  return m_prevState;
}

//-----------------------------------------------------------------------------


void ConnMonState::entry(ConnMon* monitor)
{
  TR_PRINTF(monitor->adapter()->trPort(), DbgTrace_Level::info, "FSM, entering state %s [from %s]", monitor->state()->toString(), monitor->prevState()->toString());
}

//-----------------------------------------------------------------------------

ConnMonState* ConnMonState_Unconnected::s_instance = 0;

ConnMonState* ConnMonState_Unconnected::Instance()
{
  if (0 == s_instance)
  {
    s_instance = new ConnMonState_Unconnected();
  }
  return s_instance;
}

void ConnMonState_Unconnected::evaluateState(ConnMon* monitor)
{
  if (monitor->isLanDeviceConnected())
  {
    monitor->changeState(ConnMonState_LanConnected::Instance());
  }
}

void ConnMonState_Unconnected::entry(ConnMon* monitor)
{
  ConnMonState::entry(monitor);
  monitor->adapter()->notifyLanConnected(false);
  monitor->adapter()->notifyAppProtocolConnected(false);
}

const char* ConnMonState_Unconnected::toString()
{
  return "Unconnected";
}

//-----------------------------------------------------------------------------

ConnMonState* ConnMonState_LanConnected::s_instance = 0;

ConnMonState* ConnMonState_LanConnected::Instance()
{
  if (0 == s_instance)
  {
    s_instance = new ConnMonState_LanConnected();
  }
  return s_instance;
}

void ConnMonState_LanConnected::evaluateState(ConnMon* monitor)
{
  if (!monitor->isLanDeviceConnected())
  {
    monitor->changeState(ConnMonState_Unconnected::Instance());
  }
}

void ConnMonState_LanConnected::timeExpired(ConnMon* monitor)
{
  if (monitor->isLanDeviceConnected())
  {
    monitor->changeState(ConnMonState_StableLanConnection::Instance());
  }
  else
  {
    monitor->changeState(ConnMonState_Unconnected::Instance());
  }
}

void ConnMonState_LanConnected::entry(ConnMon* monitor)
{
  ConnMonState::entry(monitor);
  monitor->startStableLanConnCheckTimer();
}

const char* ConnMonState_LanConnected::toString()
{
  return "LanConnected";
}

//-----------------------------------------------------------------------------

ConnMonState* ConnMonState_StableLanConnection::s_instance = 0;

ConnMonState* ConnMonState_StableLanConnection::Instance()
{
  if (0 == s_instance)
  {
    s_instance = new ConnMonState_StableLanConnection();
  }
  return s_instance;
}

void ConnMonState_StableLanConnection::evaluateState(ConnMon* monitor)
{
  if (monitor->isLanDeviceConnected())
  {
    if (monitor->isAppProtocolLibConnected())
    {
      monitor->changeState(ConnMonState_AppProtocolConnected::Instance());
    }
  }
  else
  {
    monitor->changeState(ConnMonState_Unconnected::Instance());
  }
}

void ConnMonState_StableLanConnection::evaluateState(ConnMon* monitor, bool appProtocolState)
{
  if (appProtocolState)
  {
    monitor->changeState(ConnMonState_AppProtocolConnected::Instance());
  }
}

void ConnMonState_StableLanConnection::entry(ConnMon* monitor)
{
  ConnMonState::entry(monitor);
  monitor->adapter()->notifyLanConnected(true);
}

const char* ConnMonState_StableLanConnection::toString()
{
  return "StableLanConnection";
}

//-----------------------------------------------------------------------------

ConnMonState* ConnMonState_AppProtocolConnected::s_instance = 0;

ConnMonState* ConnMonState_AppProtocolConnected::Instance()
{
  if (0 == s_instance)
  {
    s_instance = new ConnMonState_AppProtocolConnected();
  }
  return s_instance;
}

void ConnMonState_AppProtocolConnected::evaluateState(ConnMon* monitor)
{
  if (monitor->isLanDeviceConnected())
  {
    if (!monitor->isAppProtocolLibConnected())
    {
      monitor->changeState(ConnMonState_StableLanConnection::Instance());
      monitor->adapter()->notifyAppProtocolConnected(false);
    }
  }
  else
  {
    monitor->changeState(ConnMonState_Unconnected::Instance());
  }
}

void ConnMonState_AppProtocolConnected::evaluateState(ConnMon* monitor, bool mqttState)
{
  if (!mqttState)
  {
    monitor->changeState(ConnMonState_LanConnected::Instance());
  }
}

void ConnMonState_AppProtocolConnected::entry(ConnMon* monitor)
{
  ConnMonState::entry(monitor);
  monitor->adapter()->notifyAppProtocolConnected(true);
}

const char* ConnMonState_AppProtocolConnected::toString()
{
  return "AppProtocolConnected";
}
