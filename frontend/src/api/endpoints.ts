/**
 * API endpoint constants
 */

// Backend currently exposes APIs at `/api` (no version). If the backend later adds a versioned prefix,
// update this constant accordingly.
const API_PREFIX = `/api`;

export const API_ENDPOINTS = {
  // System endpoints
  SYSTEM_INFO: `${API_PREFIX}/sys/info`,
  SYSTEM_REBOOT: `${API_PREFIX}/sys/reboot`,
  SYSTEM_FACTORY: `${API_PREFIX}/sys/factory`,
  SYSTEM_OTA: `${API_PREFIX}/sys/ota`,
  SYSTEM_HEALTH: `${API_PREFIX}/sys/health`,

  // DMX endpoints
  DMX_STATUS: `${API_PREFIX}/dmx/status`,
  DMX_CONFIG: `${API_PREFIX}/dmx/config`,

  // Network endpoints
  NETWORK_STATUS: `${API_PREFIX}/net/status`,
  NETWORK_CONFIG: `${API_PREFIX}/net/config`,

  // Auth endpoints
  AUTH_LOGIN: `${API_PREFIX}/auth/login`,
  AUTH_SET_PASSWORD: `${API_PREFIX}/auth/set_password`,


  // File endpoints (optional)
  FILE_EXPORT: `${API_PREFIX}/file/export`,
  FILE_IMPORT: `${API_PREFIX}/file/import`,
} as const;

