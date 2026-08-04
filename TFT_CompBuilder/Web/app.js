const state = {
  sets: [],
  emblems: [],
  jobId: null,
  job: null,
  page: 0,
  pageSize: 100,
  pollTimer: null,
};

const elements = {
  form: document.querySelector('#build-form'),
  set: document.querySelector('#set-select'),
  setMeta: document.querySelector('#set-meta'),
  size: document.querySelector('#size-input'),
  sizeOutput: document.querySelector('#size-output'),
  timeout: document.querySelector('#timeout-input'),
  emblemInput: document.querySelector('#emblem-input'),
  emblemOptions: document.querySelector('#trait-options'),
  emblemList: document.querySelector('#emblem-list'),
  addEmblem: document.querySelector('#add-emblem'),
  connected: document.querySelector('#connected-input'),
  refresh: document.querySelector('#refresh-input'),
  useCache: document.querySelector('#cache-input'),
  build: document.querySelector('#build-button'),
  statusCard: document.querySelector('#status-card'),
  statusKicker: document.querySelector('#status-kicker'),
  statusTitle: document.querySelector('#status-title'),
  statusMessage: document.querySelector('#status-message'),
  statusTime: document.querySelector('#status-time'),
  summary: document.querySelector('#result-summary'),
  resultBody: document.querySelector('#result-body'),
  resultFilter: document.querySelector('#result-filter'),
  previous: document.querySelector('#previous-page'),
  next: document.querySelector('#next-page'),
  pageLabel: document.querySelector('#page-label'),
  cacheSize: document.querySelector('#cache-size'),
  cacheCompositions: document.querySelector('#cache-compositions'),
  cacheOrphans: document.querySelector('#cache-orphans'),
  cacheMeter: document.querySelector('#cache-meter-fill'),
  cacheLimit: document.querySelector('#cache-limit'),
  cacheMessage: document.querySelector('#cache-message'),
  refreshCache: document.querySelector('#refresh-cache'),
  pruneCache: document.querySelector('#prune-cache'),
};

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: { 'Content-Type': 'application/json', ...(options.headers || {}) },
  });
  const document = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(document.error || `Request failed with status ${response.status}.`);
  return document;
}

function currentSet() {
  return state.sets.find((set) => set.id === elements.set.value);
}

function updateSetDetails() {
  const selected = currentSet();
  if (!selected) return;
  elements.setMeta.textContent = `${selected.champion_count} champions · ${selected.trait_count} multi-champion traits`;
  elements.emblemOptions.replaceChildren(...selected.traits.map((trait) => {
    const option = document.createElement('option');
    option.value = trait;
    return option;
  }));
  state.emblems = state.emblems.filter((emblem) => selected.traits.includes(emblem));
  renderEmblems();
}

async function loadSets() {
  try {
    const catalog = await api('/api/sets');
    state.sets = catalog.sets;
    elements.set.replaceChildren(...state.sets.map((set) => {
      const option = document.createElement('option');
      option.value = set.id;
      option.textContent = `Set ${set.id}`;
      option.selected = set.id === catalog.latest;
      return option;
    }));
    if (!state.sets.length) throw new Error('No valid local sets were found.');
    updateSetDetails();
  } catch (error) {
    setStatus('failed', 'Set data unavailable', error.message, '—');
    elements.build.disabled = true;
  }
}

function renderEmblems() {
  elements.emblemList.replaceChildren(...state.emblems.map((emblem, index) => {
    const chip = document.createElement('span');
    chip.className = 'trait-chip';
    chip.append(document.createTextNode(emblem));
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.setAttribute('aria-label', `Remove ${emblem} emblem`);
    remove.textContent = '×';
    remove.addEventListener('click', () => {
      state.emblems.splice(index, 1);
      renderEmblems();
    });
    chip.append(remove);
    return chip;
  }));
}

function addEmblem() {
  const emblem = elements.emblemInput.value.trim();
  const selected = currentSet();
  if (!emblem || !selected) return;
  const canonical = selected.traits.find((trait) => trait.toLowerCase() === emblem.toLowerCase());
  if (!canonical) {
    elements.emblemInput.setCustomValidity('Choose a trait from the current set.');
    elements.emblemInput.reportValidity();
    return;
  }
  elements.emblemInput.setCustomValidity('');
  state.emblems.push(canonical);
  elements.emblemInput.value = '';
  renderEmblems();
}

function setStatus(kind, title, message, time) {
  elements.statusCard.className = `status-card ${kind}`;
  elements.statusKicker.textContent = kind === 'running' ? 'Working locally' : kind === 'complete' ? 'Complete' : kind === 'failed' ? 'Needs attention' : 'Ready';
  elements.statusTitle.textContent = title;
  elements.statusMessage.textContent = message;
  elements.statusTime.textContent = time;
}

function formatTime(seconds) {
  if (!Number.isFinite(seconds)) return '—';
  if (seconds < 60) return `${seconds.toFixed(1)}s`;
  const minutes = Math.floor(seconds / 60);
  return `${minutes}m ${Math.round(seconds % 60)}s`;
}

function formatBytes(bytes) {
  const units = ['B', 'KiB', 'MiB', 'GiB'];
  let amount = Number(bytes || 0);
  let unit = 0;
  while (amount >= 1024 && unit < units.length - 1) { amount /= 1024; unit += 1; }
  return `${amount.toFixed(unit ? 1 : 0)} ${units[unit]}`;
}

function renderResults() {
  const job = state.job;
  if (!job || job.status !== 'complete') return;
  const filter = elements.resultFilter.value.trim().toLowerCase();
  const visibleResults = job.results
    .map((result, index) => ({ result, index }))
    .filter(({ result }) => !filter || result.champions.some((name) => name.toLowerCase().includes(filter)));
  elements.resultBody.replaceChildren();

  if (!visibleResults.length) {
    const row = document.createElement('tr');
    row.className = 'empty-row';
    const cell = document.createElement('td');
    cell.colSpan = 4;
    cell.textContent = filter ? 'No compositions on this page match that champion.' : 'No compositions were generated.';
    row.append(cell);
    elements.resultBody.append(row);
  } else {
    visibleResults.forEach(({ result, index }) => {
      const row = document.createElement('tr');
      const number = document.createElement('td');
      number.textContent = String(job.offset + index + 1).padStart(2, '0');
      const champions = document.createElement('td');
      const championList = document.createElement('div');
      championList.className = 'champion-list';
      result.champions.forEach((champion) => {
        const chip = document.createElement('span');
        chip.className = 'champion-chip';
        chip.textContent = champion;
        championList.append(chip);
      });
      champions.append(championList);
      const traits = document.createElement('td');
      traits.textContent = result.active_traits;
      const tiers = document.createElement('td');
      tiers.textContent = result.active_trait_tiers;
      row.append(number, champions, traits, tiers);
      elements.resultBody.append(row);
    });
  }

  const totalPages = Math.max(1, Math.ceil(job.total / state.pageSize));
  elements.pageLabel.textContent = `Page ${state.page + 1} / ${totalPages}`;
  elements.previous.disabled = state.page === 0;
  elements.next.disabled = state.page + 1 >= totalPages;
}

async function pollJob() {
  if (!state.jobId) return;
  try {
    const offset = state.page * state.pageSize;
    const job = await api(`/api/jobs/${state.jobId}?offset=${offset}&limit=${state.pageSize}`);
    state.job = job;
    if (job.status === 'queued' || job.status === 'running') {
      setStatus('running', 'Resolving compositions', job.message, formatTime(job.elapsed_seconds));
      state.pollTimer = window.setTimeout(pollJob, 750);
      return;
    }

    elements.build.disabled = false;
    if (job.status === 'failed') {
      setStatus('failed', 'Calculation failed', job.message, formatTime(job.elapsed_seconds));
      return;
    }

    setStatus('complete', `${job.total.toLocaleString()} compositions ready`, `Resolved from ${job.resolution}.`, formatTime(job.elapsed_seconds));
    elements.summary.textContent = `${job.total.toLocaleString()} size-${job.request.size} candidates · ${job.resolution}`;
    elements.resultFilter.disabled = false;
    renderResults();
    loadCache();
  } catch (error) {
    elements.build.disabled = false;
    setStatus('failed', 'Could not read job status', error.message, '—');
  }
}

async function startBuild(event) {
  event.preventDefault();
  window.clearTimeout(state.pollTimer);
  state.page = 0;
  state.job = null;
  elements.resultFilter.value = '';
  elements.resultFilter.disabled = true;
  elements.build.disabled = true;
  elements.summary.textContent = 'Calculation in progress…';
  setStatus('running', 'Starting local search', 'Preparing the selected set and cache identity.', '0.0s');

  const payload = {
    set: elements.set.value,
    size: Number(elements.size.value),
    gate_type: document.querySelector('input[name="gate-type"]:checked').value,
    emblems: state.emblems,
    connected_only: elements.connected.checked,
    gate_timeout: Number(elements.timeout.value),
    refresh: elements.refresh.checked,
    use_cache: elements.useCache.checked,
  };

  try {
    const job = await api('/api/jobs', { method: 'POST', body: JSON.stringify(payload) });
    state.jobId = job.id;
    pollJob();
  } catch (error) {
    elements.build.disabled = false;
    setStatus('failed', 'Could not start calculation', error.message, '—');
  }
}

async function changePage(direction) {
  state.page += direction;
  elements.previous.disabled = true;
  elements.next.disabled = true;
  await pollJob();
}

async function loadCache() {
  try {
    const cache = await api('/api/cache');
    elements.cacheSize.textContent = formatBytes(cache.total_bytes);
    elements.cacheCompositions.textContent = Number(cache.cached_compositions).toLocaleString();
    const cleanup = Number(cache.orphaned_objects) + Number(cache.invalid_manifests) + Number(cache.temporary_files) + Number(cache.legacy_files);
    elements.cacheOrphans.textContent = cleanup.toLocaleString();
    const objectShare = Number(cache.total_bytes) > 0 ? Number(cache.object_bytes) / Number(cache.total_bytes) : 0;
    elements.cacheMeter.style.width = `${Math.min(100, Math.max(0, objectShare * 100))}%`;
  } catch (error) {
    elements.cacheMessage.textContent = error.message;
  }
}

async function pruneCache() {
  const rawLimit = elements.cacheLimit.value.trim();
  const action = rawLimit === ''
    ? 'Remove incomplete, invalid, legacy, and unreferenced cache files?'
    : `Clean the cache and evict least-recently-used entries until it is at most ${rawLimit} MiB?`;
  if (!window.confirm(action)) return;
  elements.pruneCache.disabled = true;
  elements.cacheMessage.textContent = 'Cleaning local cache…';
  try {
    const result = await api('/api/cache/prune', {
      method: 'POST',
      body: JSON.stringify({ maximum_mb: rawLimit === '' ? null : Number(rawLimit) }),
    });
    elements.cacheMessage.textContent = `Removed ${result.removed_files} files (${formatBytes(result.removed_bytes)}).`;
    await loadCache();
  } catch (error) {
    elements.cacheMessage.textContent = error.message;
  } finally {
    elements.pruneCache.disabled = false;
  }
}

elements.size.addEventListener('input', () => { elements.sizeOutput.value = elements.size.value; });
elements.set.addEventListener('change', updateSetDetails);
elements.addEmblem.addEventListener('click', addEmblem);
elements.emblemInput.addEventListener('keydown', (event) => {
  if (event.key === 'Enter') { event.preventDefault(); addEmblem(); }
  else elements.emblemInput.setCustomValidity('');
});
elements.form.addEventListener('submit', startBuild);
elements.resultFilter.addEventListener('input', renderResults);
elements.previous.addEventListener('click', () => changePage(-1));
elements.next.addEventListener('click', () => changePage(1));
elements.refreshCache.addEventListener('click', loadCache);
elements.pruneCache.addEventListener('click', pruneCache);

loadSets();
loadCache();
