const API = window.location.origin;

// ── Auth helpers ─────────────────────────────────────
function getToken() { return localStorage.getItem('onyx_token'); }
function authHeaders() {
    return { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + getToken() };
}
function requireAuth() {
    if (!getToken()) window.location.href = '/login';
}
function logout() {
    fetch('/api/auth/logout', { method:'POST', headers: authHeaders() });
    localStorage.removeItem('onyx_token');
    localStorage.removeItem('onyx_name');
    window.location.href = '/login';
}

async function fetchTransactions() {
    try { return await (await fetch(`${API}/api/transactions`)).json(); }
    catch { return []; }
}
async function fetchSummary() {
    try { return await (await fetch(`${API}/api/summary`)).json(); }
    catch { return { totalIncome:0, totalExpenses:0, balance:0 }; }
}
async function addTransaction(data) {
    try {
        const res = await fetch(`${API}/api/transactions`, {
            method:'POST', headers:{'Content-Type':'application/json'},
            body: JSON.stringify(data)
        });
        return res.ok;
    } catch { return false; }
}
async function deleteTransaction(id) {
    try { return (await fetch(`${API}/api/transactions/${id}`, {method:'DELETE'})).ok; }
    catch { return false; }
}
async function askAI(question) {
    try {
        const res = await fetch(`${API}/api/ai/analyze`, {
            method:'POST', headers:{'Content-Type':'application/json'},
            body: JSON.stringify({question})
        });
        const d = await res.json();
        return d.error ? `Error: ${d.error}` : d.reply;
    } catch { return 'Could not reach the server.'; }
}

function fmt(n) {
    return '₹' + Math.abs(n).toLocaleString('en-IN', {minimumFractionDigits:2, maximumFractionDigits:2});
}

// ── CHARTS ──────────────────────────────────────────

const CHART_COLORS = ['#e2e8f0','#94a3b8','#64748b','#475569','#334155','#1e293b','#f8fafc','#cbd5e1'];

function buildDonut(txns) {
    const ctx = document.getElementById('donutChart');
    if (!ctx) return;
    const expenses = txns.filter(t => t.type === 'expense');
    const byCategory = {};
    expenses.forEach(t => { byCategory[t.category] = (byCategory[t.category] || 0) + t.amount; });
    const labels = Object.keys(byCategory);
    const data   = Object.values(byCategory);
    if (!labels.length) return;

    new Chart(ctx, {
        type: 'doughnut',
        data: {
            labels,
            datasets: [{
                data,
                backgroundColor: CHART_COLORS,
                borderColor: '#07080a',
                borderWidth: 3,
                hoverOffset: 6
            }]
        },
        options: {
            cutout: '72%',
            plugins: { legend: { display: false }, tooltip: {
                callbacks: { label: ctx => ` ${ctx.label}: ${fmt(ctx.raw)}` },
                backgroundColor: '#0d0f12', borderColor: '#1a1d22', borderWidth: 1,
                titleColor: '#f1f5f9', bodyColor: '#8892a4', padding: 10
            }},
            animation: { animateRotate: true, duration: 800 }
        }
    });

    // Legend
    const leg = document.getElementById('chartLegend');
    if (leg) {
        leg.innerHTML = labels.map((l,i) => `
            <div class="legend-item">
                <span class="legend-label">
                    <span class="legend-dot" style="background:${CHART_COLORS[i % CHART_COLORS.length]}"></span>
                    ${l}
                </span>
                <span class="legend-val">${fmt(data[i])}</span>
            </div>`).join('');
    }
}

function buildLine(txns) {
    const ctx = document.getElementById('lineChart');
    if (!ctx) return;
    const expenses = txns.filter(t => t.type === 'expense');
    const byDate = {};
    expenses.forEach(t => { byDate[t.date] = (byDate[t.date] || 0) + t.amount; });
    const sorted = Object.keys(byDate).sort();
    if (sorted.length < 2) return;

    new Chart(ctx, {
        type: 'line',
        data: {
            labels: sorted.map(d => d.slice(5)),
            datasets: [{
                label: 'Expenses',
                data: sorted.map(d => byDate[d]),
                borderColor: '#e2e8f0',
                backgroundColor: 'rgba(226,232,240,0.04)',
                borderWidth: 1.5,
                pointRadius: 3,
                pointBackgroundColor: '#e2e8f0',
                fill: true,
                tension: 0.4
            }]
        },
        options: {
            responsive: true,
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: { label: ctx => ` ${fmt(ctx.raw)}` },
                    backgroundColor: '#0d0f12', borderColor: '#1a1d22', borderWidth: 1,
                    titleColor: '#f1f5f9', bodyColor: '#8892a4', padding: 10
                }
            },
            scales: {
                x: { grid: { color: '#1a1d22' }, ticks: { color: '#3d4450', font: { size: 10 } } },
                y: { grid: { color: '#1a1d22' }, ticks: { color: '#3d4450', font: { size: 10 },
                    callback: v => '₹' + v.toLocaleString('en-IN') }}
            },
            animation: { duration: 800 }
        }
    });
}

// ── DASHBOARD ────────────────────────────────────────

async function loadDashboard() {
    const [txns, summary] = await Promise.all([fetchTransactions(), fetchSummary()]);

    document.getElementById('totalIncome').textContent   = fmt(summary.totalIncome);
    document.getElementById('totalExpenses').textContent = fmt(summary.totalExpenses);
    document.getElementById('balance').textContent       = fmt(summary.balance);
    if (summary.balance < 0) document.getElementById('balance').style.color = '#f87171';

    const tbody = document.getElementById('txnBody');
    if (!txns.length) {
        tbody.innerHTML = `<tr><td colspan="6"><div class="empty">
            <div class="empty-icon">◈</div>
            <p>No transactions yet. <a href="/add">Add one</a> or <a href="/import">import from bank</a>.</p>
        </div></td></tr>`;
    } else {
        tbody.innerHTML = txns.map(t => `
            <tr>
                <td class="td-date">${t.date}</td>
                <td class="td-desc">${t.description || '—'}</td>
                <td>${t.category}</td>
                <td><span class="badge ${t.type}">${t.type}</span></td>
                <td class="td-amount ${t.type}">${t.type==='income'?'+':'-'}${fmt(t.amount)}</td>
                <td><button class="btn-del" onclick="handleDelete(${t.id})">✕</button></td>
            </tr>`).join('');
    }

    buildDonut(txns);
    buildLine(txns);
}

async function handleDelete(id) {
    if (!confirm('Delete this transaction?')) return;
    if (await deleteTransaction(id)) loadDashboard();
}
