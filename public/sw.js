const CACHE_NAME = 'pump-protector-v2';
const APP_SHELL = [
  '/',
  '/index.html',
  '/signin.html',
  '/signup.html',
  '/home.html',
  '/dashboard.html',
  '/pump.html',
  '/alerts.html',
  '/profile.html',
  '/css/style.css',
  '/js/app.js',
  '/manifest.json',
  '/icon-192.png',
  '/icon-512.png'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      return cache.addAll(APP_SHELL).catch((err) => {
        console.warn('SW cache.addAll partially failed', err);
      });
    })
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(
        keys.map((key) => {
          if (key !== CACHE_NAME) return caches.delete(key);
        })
      )
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);

  // Never cache API calls — status and commands must always be fresh
  if (url.pathname.startsWith('/api/')) return;

  event.respondWith(
    caches.match(event.request).then((cached) => {
      return (
        cached ||
        fetch(event.request).catch(() => {
          if (event.request.mode === 'navigate') {
            return caches.match('/home.html') || caches.match('/index.html');
          }
          return cached;
        })
      );
    })
  );
});
