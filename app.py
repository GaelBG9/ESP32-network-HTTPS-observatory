import numpy as np
import pandas as pd
import plotly.express as px
import requests
import streamlit as st

NUMERIC_COLUMNS = ["rtt_ms", "gateway_ms", "http_ms", "http_code", "rssi_dbm"]
COLORS = {"rtt_ms": "#55b9ff", "gateway_ms": "#b197fc", "rssi_dbm": "#50dfba"}
GAP_SECONDS = 120

st.set_page_config(page_title="SitePulse", page_icon="📡", layout="wide")
st.markdown("""
<style>
.stApp {background: #0c1222}
div[data-testid="stMetric"] {
    background: #172238; padding: 20px; border-radius: 16px;
    border: 1px solid #283752
}
h1 {color: #63ded2}
</style>
""", unsafe_allow_html=True)
st.title("SitePulse")
st.caption("ESP32 Network & HTTPS Observatory · Gaël Boënnec")
mode = st.sidebar.radio("Source", ["Démonstration", "Google Sheets"])
hours = st.sidebar.selectbox("Période · heures", [1, 6, 24, 72, 168], index=2)
alert_minutes = st.sidebar.number_input(
    "Alerte après · minutes", min_value=2, max_value=180, value=35, step=1
)
st.sidebar.caption("Contrôle automatique toutes les 60 s tant que la page est active.")


@st.cache_data(ttl=55, show_spinner=False)
def fetch(url, token):
    response = requests.post(url, json={"action": "read", "token": token}, timeout=60)
    response.raise_for_status()
    payload = response.json()
    if not payload.get("ok"):
        raise ValueError(payload.get("error", "Réponse invalide"))
    return pd.DataFrame(payload["rows"])


def demo():
    rng = np.random.default_rng(42)
    end = pd.Timestamp.now(tz="UTC").floor("30min")
    starts = pd.date_range(end=end, periods=48, freq="30min")
    stamps = pd.DatetimeIndex([
        start + pd.Timedelta(seconds=sample)
        for start in starts for sample in range(60)
    ])
    count = len(stamps)
    ok = rng.random(count) > 0.025
    rtt = rng.normal(15, 3, count).clip(3)
    rtt[1200:1260] += 45
    return pd.DataFrame({
        "id": [f"demo_{i}" for i in range(count)],
        "timestamp": stamps, "device": "demo",
        "rtt_ms": np.where(ok, rtt, np.nan),
        "gateway_ms": rng.uniform(1, 12, count),
        "status": np.where(ok, "ok", "timeout"),
        "rssi_dbm": rng.normal(-45, 3, count),
        "http_ms": np.where(np.arange(count) % 60 == 59, rng.uniform(150, 450, count), np.nan),
        "http_code": np.where(np.arange(count) % 60 == 59, 403, np.nan),
    })


def prepare(frame):
    frame = frame.copy()
    if "id" in frame:
        duplicate = frame["id"].notna() & frame["id"].ne("") & frame["id"].duplicated()
        frame = frame.loc[~duplicate].copy()
    frame["timestamp"] = pd.to_datetime(frame["timestamp"], utc=True, errors="coerce")
    frame = frame.dropna(subset=["timestamp"]).sort_values("timestamp", kind="stable")
    if "device" not in frame:
        frame["device"] = "inconnue"
    frame["device"] = frame["device"].fillna("inconnue").astype(str)
    frame["recovered"] = frame["device"].str.endswith("_recovered")
    frame["sonde"] = frame["device"].str.replace(r"_recovered$", "", regex=True)
    for column in NUMERIC_COLUMNS:
        frame[column] = pd.to_numeric(
            frame.get(column, pd.Series(index=frame.index, dtype=float)), errors="coerce"
        )
    if "status" not in frame:
        frame["status"] = "error"
    frame["heure"] = frame["timestamp"].dt.tz_convert("Europe/Paris")
    return frame


def assign_sessions(frame):
    frame = frame.sort_values("timestamp", kind="stable").copy()
    # Le changement de source distingue aussi les sessions reconstruites.
    boundary = (
        frame["timestamp"].diff().dt.total_seconds().gt(GAP_SECONDS)
        | frame["recovered"].ne(frame["recovered"].shift())
    )
    frame["session"] = boundary.cumsum()
    return frame


def session_summary(frame):
    records = []
    for session, group in frame.groupby("session", sort=True):
        valid = group.loc[group["status"].eq("ok"), "rtt_ms"].dropna()
        sent = group["status"].isin(["ok", "timeout"]).sum()
        records.append({
            "session": session,
            "début": group["heure"].min(),
            "médiane": valid.median(),
            "p95": valid.quantile(0.95),
            "mesures": len(group),
            "sans réponse (%)": 100 * group["status"].eq("timeout").sum() / sent if sent else np.nan,
            "récupérée": bool(group["recovered"].any()),
        })
    return pd.DataFrame(records)


def chart(frame, columns, title, key):
    long = frame.melt(
        id_vars=["heure", "session", "recovered"],
        value_vars=columns, var_name="mesure", value_name="valeur"
    )
    # Chaque groupe = un segment. Aucun raccord entre sessions.
    # Les lignes recuperees ont une heure commune: points uniquement.
    regular = long[~long["recovered"]]
    fig = px.line(
        regular, x="heure", y="valeur", color="mesure", line_group="session",
        markers=True, title=title, color_discrete_map=COLORS, template="plotly_dark"
    )
    recovered = long[long["recovered"] & long["valeur"].notna()]
    for metric, group in recovered.groupby("mesure"):
        fig.add_scatter(
            x=group["heure"], y=group["valeur"], mode="markers",
            name=f"{metric} · récupéré", marker={"color": COLORS.get(metric), "symbol": "diamond"},
        )
    fig.update_traces(connectgaps=False)
    fig.update_layout(
        paper_bgcolor="#0c1222", plot_bgcolor="#0c1222", legend_title_text="",
        xaxis_title="Heure de Paris", yaxis_title="",
    )
    st.plotly_chart(fig, use_container_width=True, key=key)


@st.fragment(run_every="60s")
def dashboard():
    if st.button("Actualiser maintenant"):
        fetch.clear()
    if mode == "Démonstration":
        st.info("DONNÉES SIMULÉES")
        raw = demo()
    else:
        try:
            raw = fetch(st.secrets["endpoint"], st.secrets["read_token"])
        except Exception:
            st.error(
                "Lecture Google Sheets impossible. L'état de la sonde ne peut pas être "
                "vérifié. Contrôle la connexion, le déploiement et les secrets."
            )
            return
    if raw.empty:
        st.warning("Aucune mesure reçue.")
        return
    frame = prepare(raw)
    if frame.empty:
        st.warning("Aucun horodatage valide.")
        return

    devices = sorted(frame["sonde"].unique())
    default_index = devices.index("esp32_casa") if "esp32_casa" in devices else 0
    device = st.selectbox("Sonde", devices, index=default_index)
    frame = frame.loc[frame["sonde"].eq(device)].copy()
    now = pd.Timestamp.now(tz="UTC")
    # Ce calcul precede le filtre de periode et inclut les echecs de ping.
    latest = frame["timestamp"].max()
    age_seconds = (now - latest).total_seconds()
    latest_text = latest.tz_convert("Europe/Paris").strftime("%d/%m/%Y %H:%M:%S")
    if age_seconds < -300:
        st.warning("La dernière mesure est datée dans le futur. Vérifie l'horloge de la sonde.")
    elif age_seconds > alert_minutes * 60:
        st.error(
            f"Aucune nouvelle mesure depuis {age_seconds / 60:.0f} min "
            f"(seuil {alert_minutes} min). Dernière mesure : {latest_text}."
        )
    else:
        st.success(
            f"Mesures à jour · dernière mesure : {latest_text} "
            f"· il y a {max(0, age_seconds) / 60:.0f} min"
        )
    st.caption(
        "L'alerte porte sur l'âge des mesures enregistrées. Un retard peut venir "
        "de la sonde, du Wi-Fi ou de l'envoi. Les pauses prévues entre sessions "
        "ne sont pas comptées comme des pertes de paquets."
    )

    # Grouper avant le filtre evite de fabriquer un nouveau debut de session.
    frame = assign_sessions(frame)
    frame = frame[frame["timestamp"] >= now - pd.Timedelta(hours=hours)].copy()
    if frame.empty:
        st.warning("Aucune mesure dans cette période.")
        return
    valid = frame.loc[frame["status"].eq("ok"), "rtt_ms"].dropna()
    sent = frame["status"].isin(["ok", "timeout"]).sum()
    a, b, c, d = st.columns(4)
    a.metric("RTT médian", f"{valid.median():.1f} ms" if len(valid) else "—")
    b.metric("RTT p95", f"{valid.quantile(.95):.1f} ms" if len(valid) else "—")
    c.metric("Pings sans réponse", f"{100 * frame['status'].eq('timeout').sum() / sent:.2f} %" if sent else "—")
    d.metric("Mesures", f"{len(frame):,}")

    if frame["recovered"].any():
        st.caption(
            "Les mesures récupérées sont incluses avec la sonde d'origine. "
            "Leur heure correspond au début de session. Elles apparaissent en losanges dans le détail."
        )
    summary = session_summary(frame)
    overview, detail, https, data = st.tabs([
        "Sessions", "Détail & Wi-Fi", "HTTPS", "Données & incidents"
    ])
    with overview:
        points = summary.melt(
            id_vars=["début", "mesures", "sans réponse (%)", "récupérée"],
            value_vars=["médiane", "p95"], var_name="indicateur", value_name="RTT (ms)"
        )
        st.plotly_chart(
            px.scatter(
                points, x="début", y="RTT (ms)", color="indicateur",
                hover_data=["mesures", "sans réponse (%)", "récupérée"],
                title="RTT par session", template="plotly_dark",
                color_discrete_sequence=["#55b9ff", "#ffc66d"],
            ),
            use_container_width=True, key="session_rtt",
        )
        st.plotly_chart(
            px.bar(
                summary, x="début", y="sans réponse (%)",
                title="Pings sans réponse par session", template="plotly_dark",
                color_discrete_sequence=["#ff718b"],
            ),
            use_container_width=True, key="session_loss",
        )
        st.dataframe(summary.drop(columns="session"), use_container_width=True)
    with detail:
        options = ["Toutes les sessions"] + summary["session"].tolist()
        labels = {
            row["session"]: f"{row['début']:%d/%m %H:%M:%S} · {row['mesures']} mesures"
            for _, row in summary.iterrows()
        }
        selection = st.selectbox(
            "Session à examiner", options,
            format_func=lambda value: labels.get(value, value),
        )
        selected = frame if selection == "Toutes les sessions" else frame[frame["session"].eq(selection)]
        chart(selected, ["rtt_ms", "gateway_ms"], "RTT · millisecondes", "rtt_detail")
        chart(selected, ["rssi_dbm"], "Signal Wi-Fi · dBm", "rssi_detail")
        st.caption("Les courbes s'arrêtent entre sessions. Choisis une session pour examiner sa minute de mesures.")
    with https:
        measured = frame[frame["http_code"].notna()]
        if measured.empty:
            st.info("Aucune mesure HTTPS dans cette période.")
        else:
            st.plotly_chart(
                px.scatter(
                    measured, x="heure", y="http_ms", color=measured["http_code"].astype(str),
                    title="Durée HTTPS · millisecondes", template="plotly_dark",
                    labels={"color": "Code HTTP"},
                ),
                use_container_width=True, key="https_points",
            )
            st.dataframe(measured[["heure", "http_code", "http_ms"]], use_container_width=True)
        st.caption("403 indique une réponse reçue avec accès refusé. Les délais incluent le travail de la carte.")
    with data:
        failures = frame.loc[~frame["status"].eq("ok")]
        st.caption(f"{len(failures)} erreurs ou absences de réponse enregistrées.")
        st.dataframe(frame.drop(columns=["sonde", "session"]), use_container_width=True)
        export = frame.drop(columns=["heure", "sonde", "session", "recovered"])
        st.download_button(
            "Télécharger le CSV", export.to_csv(index=False).encode("utf-8"),
            "mesures.csv", "text/csv"
        )


dashboard()
